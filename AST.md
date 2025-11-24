# 단계 수정

"lexing -> (token) -> parsing -> (bytecode) -> vm"에서
"lexing -> (token) -> parsing -> (AST) -> code_gen -> (bytecode) -> vm"으로 단계 수정

---

# ⚠️ 최우선 원칙 ⚠️

## 이 문서를 읽기 전에 반드시 이해할 것

### 1. **lparser.\* 와 lcode.\* 를 EXACTLY 참고하라**

**"참고"의 의미**:
- ❌ 코드 구조만 대충 보기
- ❌ 알고리즘만 이해하기
- ✅ **라인 단위로 분석하고 똑같이 구현하기**
- ✅ **"왜 이렇게 했을까?" 항상 질문하고 이유 찾기**

**lparser.c와 lcode.c는 30년간 검증된 코드다**:
- 모든 엣지 케이스 처리됨
- 모든 에러 상황 고려됨
- 최적화와 정확성의 균형
- **당신이 더 잘할 수 없다**

### 2. **세부 로직을 절대 무시하지 마라**

```c
// 이런 한 줄도 이유가 있다:
int line = ls->linenumber;  // 토큰 소비 전에 라인 저장
luaX_next(ls);              // 토큰 소비
enterlevel(ls);             // 재귀 깊이 체크
```

**무시하면 반드시 버그 발생**:
- `print(1+2)` 같은 간단한 코드도 실패
- 디버깅에 몇 시간 소비
- 결국 lparser로 돌아옴

### 3. **AST 방식의 핵심 차이점 이해하기**

| 항목 | lparser (단일 단계) | last + lcodegen (2단계) |
|------|-------------------|------------------------|
| **파싱** | 코드 생성과 동시 진행 | AST만 생성 |
| **상태 추적** | expdesc (register, constant, jump list 등) | AstNode (구조만) |
| **코드 생성** | luaK_xxx 즉시 호출 | 나중에 AST 순회하며 생성 |
| **최적화** | 파싱 중 최적화 가능 | 코드 생성 시에만 가능 |
| **메모리** | FuncState 하나만 | AST 전체 + CodeGenState |

**이로 인한 주의사항**:
- ✅ **파싱**: lparser 구조 따르되, `luaK_xxx` 호출은 AST 노드 생성으로 대체
- ✅ **코드 생성**: lcode.c 패턴 **EXACTLY** 따르기 (expdesc 패턴 필수)
- ❌ **절대**: expdesc 없이 레지스터 번호만으로 코드 생성 시도

---

## last.\*

+ 새로운 단계에서의 parsing(즉, AST 생성)을 담당하는 last.\* 파일을 만들어서 parsing을 구현할 것.

+ syntax error 보고, enter level, 기타 등등 문법에 관한 것은 모두 last.\*에서 처리할 것. 이는 lparser.\*를 참고

### **CRITICAL: lparser.\* 참고 시 주의사항**

#### 핵심 원칙: **기본 로직뿐만 아니라 세부 로직과 부수적인 처리까지 정확히 참고**

**lparser.\*를 "참고"한다는 것의 의미:**
- ❌ BNF 구조만 보고 대충 구현
- ❌ 주요 함수 구조만 따라하기
- ✅ **세부적인 처리 로직까지 EXACTLY 따라하기**
- ✅ **에러 처리, 재귀 제한, 토큰 체크, 순서 등 모든 디테일 포함**

lparser.c의 **모든 함수에는 이유가 있다**:
- 재귀 깊이 체크 (`enterlevel`/`leavelevel`)
- 에러 메시지 일관성
- 토큰 소비 순서
- 라인 번호 기록 타이밍
- 특수 케이스 처리
- 최적화 힌트

이런 디테일들을 빠뜨리면 **반드시 버그가 발생한다**.

#### 구체적인 예시들:

##### 예시 1: 재귀 깊이 체크 (`enterlevel` / `leavelevel`)

```c
// lparser.c의 모든 재귀 함수는 이것으로 시작/종료
#define enterlevel(ls)  luaE_incCstack(ls->L)
#define leavelevel(ls)  ((ls)->L->nCcalls--)

// lparser.c:
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);  // ← 재귀 시작
  // ... parsing logic ...
  leavelevel(ls);  // ← 재귀 종료
  return op;
}

// last.c도 EXACTLY 동일하게:
static AstNode *parse_subexpr(lua_State *L, AstState *as, int limit) {
    as->ls->L->nCcalls++;  // enterlevel
    
    // ... parsing logic ...
    
    as->ls->L->nCcalls--;  // leavelevel
    return node;
}
```

**왜 필요한가:**
- 깊은 expression (예: `1+2+3+...+1000`)에서 C 스택 오버플로우 방지
- Lua가 최대 재귀 깊이를 넘으면 에러 발생
- **parse_subexpr, parse_simple_expr, parse_statement 등 모든 재귀 함수에 필수**

##### 예시 2: Operator Precedence Table

```c
// lparser.c와 EXACTLY 동일하게 유지 (한 글자도 틀리면 안 됨)
static const struct {
  lu_byte left;  /* left priority */
  lu_byte right; /* right priority */
} priority[] = {  /* ORDER OPR */
   {10, 10}, {10, 10},           /* '+' '-' */
   {11, 11}, {11, 11},           /* '*' '%' */
   {14, 13},                     /* '^' (right associative) */
   {11, 11}, {11, 11},           /* '/' '//' */
   {6, 6}, {4, 4}, {5, 5},       /* '&' '|' '~' */
   {7, 7}, {7, 7},               /* '<<' '>>' */
   {9, 8},                       /* '..' (right associative) */
   {3, 3}, {3, 3}, {3, 3},       /* ==, <, <= */
   {3, 3}, {3, 3}, {3, 3},       /* ~=, >, >= */
   {2, 2}, {1, 1}                /* and, or */
};

#define UNARY_PRIORITY 12  // ← 이것도 정확히
```

**왜 중요한가:**
- 한 숫자만 틀려도 `1+2*3`이 `(1+2)*3`으로 계산됨
- Lua 언어 명세의 일부

##### 예시 3: BNF 구조와 토큰 소비 순서

```c
// lparser.c:
// subexpr -> (simpleexp | unop subexpr) { binop subexpr }

static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  BinOpr op;
  UnOpr uop;
  enterlevel(ls);
  uop = getunopr(ls->t.token);  // ← 1. unary operator 체크
  if (uop != OPR_NOUNOPR) {
    int line = ls->linenumber;  // ← 2. 라인 번호 먼저 저장
    luaX_next(ls);               // ← 3. 그 다음 토큰 소비
    subexpr(ls, v, UNARY_PRIORITY);  // ← 4. 재귀
    luaK_prefix(ls->fs, uop, v, line);  // ← 5. 코드 생성 (last.c에서는 AST 생성)
  }
  else simpleexp(ls, v);
  
  // { binop subexpr }
  op = getbinopr(ls->t.token);
  while (op != OPR_NOBINOPR && priority[op].left > limit) {
    expdesc v2;
    BinOpr nextop;
    int line = ls->linenumber;  // ← 라인 번호 저장
    luaX_next(ls);              // ← 토큰 소비
    luaK_infix(ls->fs, op, v);  // ← 중간 처리
    // read sub-expression with higher priority
    nextop = subexpr(ls, &v2, priority[op].right);
    luaK_posfix(ls->fs, op, v, &v2, line);  // ← 코드 생성
    op = nextop;
  }
  leavelevel(ls);
  return op;
}

// last.c도 동일한 순서와 구조 유지:
static AstNode *parse_subexpr(lua_State *L, AstState *as, int limit) {
    AstNode *left;
    UnOpr uop;
    
    as->ls->L->nCcalls++;  // enterlevel
    
    uop = getunopr(as->ls->t.token);  // ← 1. unary operator 체크
    if (uop != OPR_NOUNOPR) {
        int line = as->ls->linenumber;  // ← 2. 라인 번호 먼저 저장
        luaX_next(as->ls);               // ← 3. 그 다음 토큰 소비
        left = parse_subexpr(L, as, UNARY_PRIORITY);  // ← 4. 재귀
        
        // ← 5. AST 노드 생성 (lparser의 luaK_prefix 대신)
        AstUnOp *unop = arena_alloc(L, as, sizeof(AstUnOp));
        unop->header.kind = AST_UNOP;
        unop->header.line = line;
        unop->op = uop;
        unop->operand = left;
        left = (AstNode *)unop;
    } else {
        left = parse_simple_expr(L, as);
    }
    
    // { binop subexpr }
    BinOpr op = getbinopr(as->ls->t.token);
    while (op != OPR_NOBINOPR && priority[op].left > limit) {
        int line = as->ls->linenumber;  // ← 라인 번호 저장
        AstNode *right;
        AstBinOp *binop;
        
        luaX_next(as->ls);  // ← 토큰 소비
        right = parse_subexpr(L, as, priority[op].right);
        
        // ← AST 노드 생성 (lparser의 luaK_posfix 대신)
        binop = arena_alloc(L, as, sizeof(AstBinOp));
        binop->header.kind = AST_BINOP;
        binop->header.line = line;
        binop->op = op;
        binop->left = left;
        binop->right = right;
        left = (AstNode *)binop;
        
        op = getbinopr(as->ls->t.token);
    }
    
    as->ls->L->nCcalls--;  // leavelevel
    return left;
}
```

**왜 이 순서가 중요한가:**
- 라인 번호를 토큰 소비 **전에** 저장해야 에러 메시지가 정확
- 토큰 소비 순서가 틀리면 파싱 실패
- 재귀 호출 순서가 틀리면 우선순위가 깨짐

##### 예시 4: 에러 체크와 예외 처리

```c
// lparser.c에서는 매번 확인:
static void checknext (LexState *ls, int c) {
  check(ls, c);
  luaX_next(ls);
}

static void check_match (LexState *ls, int what, int who, int where) {
  if (unlikely(!testnext(ls, what))) {
    if (where == ls->linenumber)  /* all in the same line? */
      error_expected(ls, what);  /* do not need 'match' message */
    else {
      luaX_syntaxerror(ls, luaO_pushfstring(ls->L,
         "%s expected (to close %s at line %d)",
          luaX_token2str(ls, what), luaX_token2str(ls, who), where));
    }
  }
}

// last.c도 똑같이:
static void ast_checknext(LexState *ls, int c) {
    if (ls->t.token != c)
        luaX_syntaxerror(ls, luaO_pushfstring(ls->L, 
            "%s expected", luaX_token2str(ls, c)));
    luaX_next(ls);
}
```

**왜 필요한가:**
- 사용자에게 명확한 에러 메시지 제공
- 파서가 이상한 상태로 계속 진행하는 것 방지

#### last.c와 lparser.c의 핵심 차이점

**⚠️ 중요**: lparser.c는 파싱과 코드 생성을 **동시에** 수행하지만, last.c는 **AST만** 생성한다.

```c
// lparser.c: 파싱 + 코드 생성 동시 진행
static BinOpr subexpr (LexState *ls, expdesc *v, int limit) {
  enterlevel(ls);
  // ... parsing ...
  luaK_prefix(ls->fs, uop, v, line);   // ← 즉시 코드 생성
  luaK_infix(ls->fs, op, v);           // ← 즉시 코드 생성
  luaK_posfix(ls->fs, op, &v2, v, line); // ← 즉시 코드 생성
  leavelevel(ls);
  return op;
}

// last.c: AST만 생성 (코드 생성은 lcodegen.c에서)
static AstNode *parse_subexpr(lua_State *L, AstState *as, int limit) {
  as->ls->L->nCcalls++;  // enterlevel
  // ... parsing ...
  
  // ← luaK_xxx 호출 대신 AST 노드 생성
  AstBinOp *binop = arena_alloc(as, sizeof(AstBinOp));
  binop->header.kind = AST_BINOP;
  binop->header.line = line;
  binop->op = op;
  binop->left = left;
  binop->right = right;
  
  as->ls->L->nCcalls--;  // leavelevel
  return (AstNode *)binop;
}
```

**변환 규칙**:
| lparser.c | last.c |
|-----------|--------|
| `luaK_prefix(fs, op, v, line)` | `AstUnOp` 노드 생성 |
| `luaK_infix(fs, op, v)` | (제거 - 코드 생성 없음) |
| `luaK_posfix(fs, op, v1, v2, line)` | `AstBinOp` 노드 생성 |
| `luaK_exp2nextreg(fs, e)` | (제거 - expdesc 없음) |
| `FuncState *fs` | `AstState *as` |
| `expdesc *v` | `AstNode *node` |

**⚠️ 반복 강조**: 
- last.c의 **파싱 로직**은 lparser.c와 **100% 동일**해야 함
- **코드 생성 호출**만 AST 노드 생성으로 바꿀 것
- 토큰 소비 순서, 에러 처리, enterlevel 등은 **그대로 유지**

### data structure

+ 루트 데이터 구조를 여기서는 AST라 부르겠다. 이름은 알맞게 지을 것.

+ 루트 데이터 구조:
    ```c
    typedef struct AST {
        Mbuffer arena;    // 모든 AST 노드를 위한 메모리 아레나
        ASTNode *root;    // 최상위 노드 (chunk)
    } AST;
    ```

+ 파싱 상태 (lparser의 FuncState 패턴과 동일):
    ```c
    typedef struct AstState {
        Mbuffer *arena;          // AST 노드 아레나 (참조)
        LexState *ls;            // 렉서 참조
        // 향후 확장: prev, scopelevel 등
    } AstState;
    ```

+ 모든 비단말 노드가 다음과 비슷하게 디버깅 정보를 가질 것.
    ```c
    typedef struct NodeInfo { // node header?
        int linenumber;      // 이 노드가 시작하는 소스 라인
    // ...
    } NodeInfo;

    typedef struct ASTStatement {
        NodeInfo info;
        NodeKind kind; // functioncall, label, goto, ...
    } ASTStatement;
    ```

+ 한 노드 타입에 모든 단말/비단말 구조를 넣지 말고 BNF에 맞게 설계할 것.

### 메모리 관리

+ AST 노드는 `AstState.arena` (Mbuffer)에서 할당

+ 백트래킹 시 `arena.n` 조작으로 롤백

+ **중요**: 아레나는 선형 할당만 지원하므로 **가변 길이 배열을 재할당할 수 없음**
    - 해결책: **연결 리스트** 사용 (statements, expressions 등)
    - 예시:
        ```c
        typedef struct StatementList {
            ASTNode *stmt;
            struct StatementList *next;
        } StatementList;
        
        typedef struct BlockNode {
            NodeInfo info;
            StatementList *statements;  // 연결 리스트로 관리
        } BlockNode;
        
        // 추가는 역순 (O(1), 백트래킹 안전)
        void add_statement(AstState *as, StatementList **list, ASTNode *stmt) {
            StatementList *node = arena_alloc(as, sizeof(StatementList));
            node->stmt = stmt;
            node->next = *list;
            *list = node;
        }
        
        // 반환 전에 뒤집기 (순서 복원)
        StatementList *reverse_list(StatementList *head) {
            StatementList *prev = NULL;
            while (head) {
                StatementList *next = head->next;
                head->next = prev;
                prev = head;
                head = next;
            }
            return prev;
        }
        ```
    - 모든 노드를 아레나에서 할당하므로 백트래킹 시 `arena.n` 롤백만으로 안전
    - 파싱 중에는 역순으로 추가 (O(1)), 파싱 완료 시 뒤집어서 순서 복원

+ 최종적으로 아레나 전체를 `luaD_protectedparser`에서 해제

+ 파싱 함수들은 모두 `AstState*`를 받아서 사용 (FuncState 패턴과 동일)

### 인터페이스

```c
// buff와 ast_arena는 luaD_protectedparser가 관리하고 전달
LUAI_FUNC AST *luaA_parser(lua_State *L, ZIO *z,
                           Mbuffer *lex_buff, Mbuffer *ast_arena,
                           const char *name, int firstchar);
```

## lcodegen.\*

+ 기존 단계에서 parsing(즉, bytecode 생성)은 lparser.\*가 담당하고 있음.

+ lcodegen.\*은 AST를 받아 bytecode를 생성.

+ Dyndata는 lcodegen에서만 사용 (last에서는 사용 안 함)

### **⚠️⚠️⚠️ CRITICAL: lcode.\* 를 EXACTLY 참고하라 ⚠️⚠️⚠️**

**이 섹션이 가장 중요하고 가장 어렵다.**

#### lcode.c 참고의 중요성

**lcode.c는 Lua 코드 생성의 핵심이다**:
- 30년간 다듬어진 레지스터 관리 알고리즘
- 모든 expression 타입의 최적 처리 방법
- Constant folding, jump patching, 등등
- **절대 직접 발명하지 마라**

**lcodegen.c = lcode.c의 AST 버전**:
```
lparser.c:                last.c:
  parse expr ──────────>    parse expr
       │                         │
       ↓ (즉시)                  ↓ (나중에)
  lcode.c:              lcodegen.c:
    generate code           generate code from AST
```

**핵심 차이**:
| 항목 | lcode.c | lcodegen.c |
|------|---------|------------|
| **입력** | expdesc (파싱 중 생성) | AstNode (이미 생성됨) |
| **출력** | expdesc 업데이트 | ExpInfo 생성 |
| **타이밍** | 파싱과 동시 | AST 순회 중 |
| **상태** | FuncState | CodeGenState |

**⚠️ 하지만 로직은 100% 동일해야 함**:
- expdesc 상태 머신 → ExpInfo 상태 머신 (동일)
- 레지스터 할당 전략 (동일)
- Constant 처리 방식 (동일)
- Jump patching 메커니즘 (동일)

### **CRITICAL: 코드 생성 전략 (실패 경험에서 배운 교훈)**

#### ❌ 실패한 접근 (절대 하지 말 것):

```c
// BAD: target register만 받는 단순 접근
static void gen_expr(CodeGenState *cg, AstNode *node, int target) {
    switch (node->kind) {
        case AST_BINOP:
            // 문제: left/right를 어디 레지스터에 생성할지 불명확
            // freereg를 마구 조작하다가 꼬임
            int left_reg = cg->freereg;  // ← 위험!
            int right_reg = cg->freereg + 1;  // ← 위험!
            gen_expr(cg, binop->left, left_reg);
            gen_expr(cg, binop->right, right_reg);
            // ...
    }
}
```

**왜 실패했는가:**
1. Expression이 어디에 있는지 추적 불가 (register? constant? local variable?)
2. `freereg` 관리가 caller/callee 간 불명확 → 버그 양산
3. Constant folding, register 최적화 불가능
4. `print(1+2)` 같은 간단한 코드도 실패

#### ✅ 올바른 접근: expdesc 패턴

**핵심 아이디어**: Expression의 "상태"를 추적

```c
// lcode.h에서 발췌 (정확한 구조 참고)
typedef enum {
  VVOID,        /* no value */
  VNIL,
  VTRUE,
  VFALSE,
  VK,           /* constant; info = index of constant in 'k' */
  VKFLT,        /* floating constant; nval = numerical float value */
  VKINT,        /* integer constant; ival = numerical integer value */
  VKSTR,        /* string constant; strval = TString address */
  VNONRELOC,    /* expression has its value in a fixed register;
                   info = result register */
  VLOCAL,       /* local variable; var.ridx = register index;
                   var.vidx = relative index in 'actvar.arr'  */
  VUPVAL,       /* upvalue variable; info = index of upvalue in 'upvalues' */
  VCONST,       /* compile-time <const> variable;
                   info = absolute index in 'actvar.arr'  */
  VINDEXED,     /* indexed variable;
                   ind.t = table register;
                   ind.idx = key's R index */
  VINDEXUP,     /* indexed upvalue;
                   ind.t = table upvalue;
                   ind.idx = key's K index */
  VINDEXI,      /* indexed variable with constant integer;
                   ind.t = table register;
                   ind.idx = key's value */
  VINDEXSTR,    /* indexed variable with literal string;
                   ind.t = table register;
                   ind.idx = key's K index */
  VJMP,         /* expression is a test/comparison;
                   info = pc of corresponding jump instruction */
  VRELOC,       /* expression can put result in any register;
                   info = instruction pc */
  VCALL,        /* expression is a function call; info = instruction pc */
  VVARARG       /* vararg expression; info = instruction pc */
} expkind;

typedef struct expdesc {
  expkind k;
  union {
    lua_Integer ival;    /* for VKINT */
    lua_Number nval;     /* for VKFLT */
    TString *strval;     /* for VKSTR */
    int info;            /* for generic use */
    struct {  /* for indexed variables */
      short idx;  /* index (R or "long" K) */
      lu_byte t;  /* table (register or upvalue) */
    } ind;
    struct {  /* for local variables */
      lu_byte ridx;  /* register holding the variable */
      unsigned short vidx;  /* compiler index (in 'actvar.arr')  */
    } var;
  } u;
  int t;  /* patch list of 'exit when true' */
  int f;  /* patch list of 'exit when false' */
} expdesc;
```

**lcodegen.c에서 구현할 방식:**

```c
// lcodegen.h
typedef struct ExpInfo {
    expkind kind;
    union {
        int reg;           // VNONRELOC: register number
        int kidx;          // VK: constant index
        lua_Integer ival;  // VKINT
        lua_Number nval;   // VKFLT
        struct {
            int table_reg;
            int key_reg;
        } indexed;
    } u;
    // jump patch lists for boolean expressions
    int t;  /* list of jumps to 'then' */
    int f;  /* list of jumps to 'else' */
} ExpInfo;

// lcodegen.c 핵심 함수들 (lcode.c 패턴 따르기)
static void gen_expr(CodeGenState *cg, AstNode *node, ExpInfo *e);
static void exp2nextreg(CodeGenState *cg, ExpInfo *e);
static void exp2anyreg(CodeGenState *cg, ExpInfo *e);
static void exp2val(CodeGenState *cg, ExpInfo *e);
static int exp2RK(CodeGenState *cg, ExpInfo *e);

// 사용 예시:
static void gen_binop(CodeGenState *cg, AstBinOp *binop, ExpInfo *e) {
    ExpInfo e1, e2;
    
    // 1. Generate left operand
    gen_expr(cg, binop->left, &e1);
    
    // 2. Prepare for binary operation (lcode.c의 luaK_infix 참고)
    exp2RK(cg, &e1);  // left를 register or K로 변환
    
    // 3. Generate right operand
    gen_expr(cg, binop->right, &e2);
    exp2RK(cg, &e2);  // right를 register or K로 변환
    
    // 4. Emit binary instruction
    int result_reg = cg->freereg;
    cg->freereg++;
    code_instruction(cg, CREATE_ABCk(op, result_reg, e1.u.reg, e2.u.reg, 0), 
                     binop->header.line);
    
    // 5. Result is in result_reg
    e->kind = VNONRELOC;
    e->u.reg = result_reg;
}

static void gen_local_decl(CodeGenState *cg, AstLocalDecl *decl) {
    // lparser.c의 localstat + adjust_assign 패턴 참고
    int nvars = 0;
    int nexps = 0;
    int base = cg->freereg;
    
    // Count variables
    for (NameList *n = decl->names; n; n = n->next)
        nvars++;
    
    // Reserve registers for variables
    cg->freereg += nvars;
    
    // Generate expressions
    ExpInfo e;
    for (ExprList *v = decl->values; v && nexps < nvars; v = v->next) {
        gen_expr(cg, v->expr, &e);
        exp2nextreg(cg, &e);  // ← 핵심! discharge to next register
        nexps++;
    }
    
    // Fill remaining with nil
    if (nexps < nvars) {
        code_instruction(cg, CREATE_ABx(OP_LOADNIL, base + nexps, nvars - nexps - 1), 
                        decl->header.line);
    }
    
    // Adjust freereg if needed
    cg->freereg = base + nvars;
    
    // Activate variables (add to locals table)
    // ...
}
```

#### 필수 참고: lcode.c 핵심 함수들

```c
// 이 함수들의 로직을 EXACTLY 따를 것:
void luaK_exp2nextreg(FuncState *fs, expdesc *e);  // discharge to fs->freereg++
int luaK_exp2anyreg(FuncState *fs, expdesc *e);    // discharge to any register
void luaK_exp2val(FuncState *fs, expdesc *e);      // to register or constant
int luaK_exp2RK(FuncState *fs, expdesc *e);        // to RK operand
void luaK_prefix(FuncState *fs, UnOpr op, expdesc *e, int line);
void luaK_infix(FuncState *fs, BinOpr op, expdesc *v);
void luaK_posfix(FuncState *fs, BinOpr op, expdesc *e1, expdesc *e2, int line);
```

#### freereg 관리 원칙

```c
// RULE 1: freereg는 "다음 사용 가능한 임시 레지스터"를 가리킴
// RULE 2: 오직 statement-level에서만 freereg 조정
// RULE 3: expression 내부에서는 exp2nextreg로만 freereg 증가

// BAD:
static void gen_expr(CodeGenState *cg, AstNode *node, int target) {
    int saved = cg->freereg;  // ← 매번 save/restore는 잘못된 설계
    // ...
    cg->freereg = saved;  // ← 매번 save/restore는 잘못된 설계
}

// GOOD:
static void gen_expr(CodeGenState *cg, AstNode *node, ExpInfo *e) {
    // freereg는 필요할 때만 증가 (exp2nextreg 호출 시)
    // caller가 관리
}

static void gen_statement(CodeGenState *cg, AstNode *node) {
    int base = cg->freereg;  // statement 시작 시점 기록
    
    // ... statement 처리 ...
    
    cg->freereg = base;  // statement 종료 시 복원
}
```

### 인터페이스

```c
// dyd는 luaD_protectedparser가 관리하고 전달
LUAI_FUNC LClosure *luaY_generate(lua_State *L, AST *ast,
                                  Dyndata *dyd, const char *name);
```

## llex.\*

+ lparser를 위한 필드를 지울 것:
    - `fs` (FuncState*) 제거 - last와 lcodegen이 각자 관리
    - `dyd` (Dyndata*) 제거 - lcodegen 전용

+ 렉서는 순수하게 토큰만 제공

## 요약

### 추상 인터페이스 (luaD_protectedparser 관점)

```c
// 모든 리소스를 luaD_protectedparser가 관리 (pcall 에러 처리)
struct SParser {
    ZIO *z;
    Mbuffer lex_buff;      // 렉서용
    Mbuffer ast_arena;     // AST용
    Dyndata dyd;           // 코드생성용
    const char *mode;
    const char *name;
};

TStatus luaD_protectedparser(lua_State *L, ZIO *z, 
                             const char *name, const char *mode) {
    struct SParser p;
    
    // 리소스 초기화
    p.dyd.actvar.arr = NULL; p.dyd.actvar.size = 0;
    p.dyd.gt.arr = NULL; p.dyd.gt.size = 0;
    p.dyd.label.arr = NULL; p.dyd.label.size = 0;
    luaZ_initbuffer(L, &p.lex_buff);
    luaZ_initbuffer(L, &p.ast_arena);
    
    // protected call
    status = luaD_pcall(L, f_parser, &p, ...);
    
    // 성공/실패 무관하게 무조건 정리
    luaZ_freebuffer(L, &p.lex_buff);
    luaZ_freebuffer(L, &p.ast_arena);
    luaM_freearray(L, p.dyd.actvar.arr, p.dyd.actvar.size);
    luaM_freearray(L, p.dyd.gt.arr, p.dyd.gt.size);
    luaM_freearray(L, p.dyd.label.arr, p.dyd.label.size);
    
    return status;
}

// f_parser 내부
static void f_parser(lua_State *L, void *ud) {
    struct SParser *p = cast(struct SParser *, ud);
    
    AST *ast = luaA_parser(L, p->z, &p->lex_buff, &p->ast_arena,
                          p->name, firstchar);
    LClosure *cl = luaY_generate(L, ast, &p->dyd, p->name);
    
    luaM_free(L, ast);  // AST 구조체만 해제 (아레나는 SParser가 관리)
    return cl;
}
```

### 상세 구현 (각 함수 내부 동작)

#### luaA_parser 내부
```c
AST *luaA_parser(lua_State *L, ZIO *z,
                 Mbuffer *lex_buff, Mbuffer *ast_arena,
                 const char *name, int firstchar) {
    LexState ls;
    AstState as;
    
    // 렉서 초기화 (이미 초기화된 버퍼 사용)
    luaX_setinput(L, &ls, z, name, firstchar);
    ls.buff = lex_buff;  // 포인터만 저장
    
    // AstState 초기화 (이미 초기화된 아레나 사용)
    as.ls = &ls;
    as.arena = ast_arena;  // 포인터만 저장
    
    // 파싱 수행 (에러 시 longjmp, 정리는 luaD_protectedparser가 함)
    ASTNode *root = parse_chunk(L, &as);
    
    // AST 구조체 생성
    AST *ast = luaM_new(L, AST);
    ast->arena = ast_arena;  // 포인터 참조 (소유 안 함!)
    ast->root = root;
    
    return ast;
}

// 파싱 함수 예시 (lparser 패턴)
static ASTNode *parse_block(lua_State *L, AstState *as) {
    BlockNode *block = arena_alloc(as, sizeof(BlockNode));
    block->info.linenumber = as->ls->linenumber;
    block->statements = NULL;
    
    // 역순으로 추가 (O(1), 백트래킹 안전)
    while (!test_block_end(as)) {
        ASTNode *stmt = parse_statement(L, as);
        add_statement(as, &block->statements, stmt);
    }
    
    // 반환 전에 뒤집어서 순서 복원
    block->statements = reverse_list(block->statements);
    
    return (ASTNode*)block;
}

static ASTNode *parse_statement(lua_State *L, AstState *as) {
    // 백트래킹 예시
    size_t saved = as->arena->n;
    ASTNode *node = try_parse_expr_stmt(L, as);
    if (!node) {
        as->arena->n = saved;  // 롤백
        node = parse_other_stmt(L, as);
    }
    return node;
}
```

#### luaY_generate 내부
```c
LClosure *luaY_generate(lua_State *L, AST *ast, 
                        Dyndata *dyd, const char *name) {
    // Dyndata는 이미 초기화되어 있음 (luaD_protectedparser가 함)
    
    // AST → 바이트코드 생성
    LClosure *cl = codegen_from_ast(L, ast, dyd, name);
    
    // Dyndata 해제는 luaD_protectedparser가 함
    return cl;
}
```

---

## 구현 체크리스트

### last.c (AST 파싱) - **lparser.c를 옆에 두고 작업할 것**

**작업 방법**:
1. lparser.c의 함수를 열어둔다
2. 라인별로 읽으면서 대응되는 last.c 코드 작성
3. `luaK_xxx` 호출만 AST 노드 생성으로 변경
4. 나머지는 **모두 동일**하게

- [ ] **필수**: lparser.c의 모든 함수를 참고하여 작성
  - [ ] `subexpr` → `parse_subexpr`
  - [ ] `simpleexp` → `parse_simple_expr`
  - [ ] `statement` → `parse_statement`
  - [ ] `statlist` → `parse_statlist`
  - [ ] `localstat` → `parse_local_stmt`
  - [ ] 등등 모든 함수

- [ ] `enterlevel` / `leavelevel` 모든 재귀 함수에 적용
  - [ ] `parse_subexpr` ✓
  - [ ] `parse_simple_expr` ✓
  - [ ] `parse_statement` ✓
  - [ ] `parse_funcargs` ✓
  - [ ] 기타 모든 재귀 함수 ✓

- [ ] Operator precedence table을 lparser.c에서 **복사-붙여넣기**
  - [ ] 배열 내용 100% 동일
  - [ ] `UNARY_PRIORITY` 값 동일
  
- [ ] 토큰 소비 순서를 lparser.c와 **정확히** 동일하게
  - [ ] 라인 번호 저장 → 토큰 소비 순서
  - [ ] `check`, `checknext`, `check_match` 동일하게
  
- [ ] 에러 메시지를 lparser.c와 **동일**하게
  - [ ] `luaX_syntaxerror` 호출 위치
  - [ ] 에러 메시지 문자열

- [ ] BNF 구조 정확히 따르기
  - [ ] `subexpr -> (simpleexp | unop subexpr) { binop subexpr }`
  - [ ] `simpleexp -> ...`
  - [ ] lparser.c 주석의 BNF와 비교

- [ ] 아레나 기반 메모리 관리
  - [ ] 연결 리스트 사용 (역순 추가 → 반환 시 뒤집기)
  - [ ] 백트래킹 지원 (`arena.n` 조작)

### lcodegen.c (코드 생성) - **lcode.c를 옆에 두고 작업할 것**

**⚠️ 가장 중요**: 이 부분이 실패의 주 원인이었다. lcode.c를 **라인 단위로** 참고하라.

**작업 방법**:
1. lcode.c의 함수를 열어둔다 (예: `luaK_exp2nextreg`)
2. 로직을 **정확히** 이해한다
3. ExpInfo로 변환하되, **알고리즘은 100% 동일**하게
4. expdesc → ExpInfo, FuncState → CodeGenState만 변경

- [ ] **ExpInfo 구조체 정의** - lcode.h의 expdesc **복사**
  - [ ] `expkind` enum을 lcode.h에서 복사
  - [ ] union 구조를 lcode.h와 동일하게
  - [ ] jump patch lists (t, f) 포함
  - [ ] **참고**: `lcode.h` 의 expdesc 정의

- [ ] **핵심 함수들 구현** - lcode.c의 luaK_xxx 함수들을 변환
  - [ ] `gen_expr` ← AstNode 순회 + ExpInfo 생성
  - [ ] `exp2nextreg` ← `luaK_exp2nextreg` 로직 복사
  - [ ] `exp2anyreg` ← `luaK_exp2anyreg` 로직 복사
  - [ ] `exp2val` ← `luaK_exp2val` 로직 복사
  - [ ] `exp2RK` ← `luaK_exp2RK` 로직 복사
  - [ ] `dischargevars` ← `luaK_dischargevars` 로직 복사

- [ ] **Binary/Unary 연산** - lcode.c의 codearith, codeunary 참고
  - [ ] `gen_prefix` ← lcode.c의 `luaK_prefix` 로직
  - [ ] `gen_infix` ← lcode.c의 `luaK_infix` 로직
  - [ ] `gen_posfix` ← lcode.c의 `luaK_posfix` 로직
  - [ ] `codearith` 패턴 (constant folding 포함)
  - [ ] **참고**: `lcode.c` 라인 1500-1700

- [ ] **Boolean expression과 jump patching**
  - [ ] `goiftrue`, `goiffalse` ← lcode.c의 동일 함수
  - [ ] `patchlistaux`, `patchtohere` ← lcode.c
  - [ ] Short-circuit evaluation (AND, OR)
  - [ ] **참고**: `lcode.c` 라인 400-600

- [ ] **freereg 관리 원칙** - lcode.c 패턴
  - [ ] Statement-level에서만 freereg 저장/복원
  - [ ] Expression에서는 exp2nextreg로만 증가
  - [ ] **절대 매 함수마다 save/restore 하지 말 것**
  - [ ] lcode.c의 freereg 조작을 **정확히** 따르기

- [ ] **Local variable 관리** - lparser.c의 localstat 참고
  - [ ] Register 예약 → expression 생성 → 변수 활성화 순서
  - [ ] `adjust_assign` 패턴 (값 개수 조정)
  - [ ] **참고**: `lparser.c` 의 `localstat` 함수

- [ ] **Function call 처리**
  - [ ] 함수 + 인자를 연속 레지스터에 배치
  - [ ] OP_CALL 생성
  - [ ] 결과 레지스터 처리
  - [ ] **참고**: `lcode.c`의 `luaK_setcallreturns`

### 테스트 순서

1. [ ] `print(3)` - 간단한 함수 호출
2. [ ] `local x = 5; print(x)` - local 변수
3. [ ] `print(1+2)` - 표현식 인자
4. [ ] `local x = 1+2; print(x)` - binop + local
5. [ ] `local a=1; local b=2; local c=a+b; print(c)` - 복합
6. [ ] Parenthesized expressions: `print((1+2)*3)`
7. [ ] Boolean operators: `print(true and false)`
8. [ ] Comparison: `print(1 < 2)`
9. [ ] Function definition and call
10. [ ] Table constructor and indexing

### 절대 하지 말 것 (Anti-patterns)

❌ `gen_expr(node, int target)` - target register 직접 전달  
❌ 매 함수마다 `saved_freereg = cg->freereg; ... cg->freereg = saved_freereg;`  
❌ expdesc 없이 레지스터 번호만으로 관리  
❌ freereg를 expression 생성 중 임의로 조작  
❌ lparser의 코드 생성 호출(luaK_xxx) 부분을 AST 생성에 포함  

### 참고해야 할 lcode.c 함수들 (우선순위)

1. **필수**: `luaK_exp2nextreg`, `luaK_exp2anyreg`, `luaK_exp2RK`
2. **중요**: `luaK_prefix`, `luaK_infix`, `luaK_posfix`
3. **유용**: `luaK_nil`, `luaK_setoneret`, `luaK_dischargevars`
4. **고급**: `luaK_goiftrue`, `luaK_goiffalse` (boolean expression용)

---

## 핵심 교훈 요약

### ⚠️ 가장 중요한 것 (반드시 기억하라)

**1. lparser.c와 lcode.c는 30년간 검증된 정답이다**
   - 당신이 더 잘 만들 수 없다
   - 당신이 더 간단하게 만들 수 없다
   - 당신이 발견한 "더 나은 방법"은 이미 시도되었고 실패했다
   - **겸손하게 따르라**

**2. "참고"의 의미는 "복사"에 가깝다**
   - ❌ 아이디어만 가져오기
   - ❌ 알고리즘만 이해하기
   - ✅ **라인 단위로 분석하고 변환하기**
   - ✅ **모든 디테일(enterlevel, 라인 번호, 에러 체크 등) 포함**

**3. AST 방식의 차이점을 정확히 이해하라**
   
   **파싱 (last.c)**:
   - lparser.c와 **100% 동일**한 파싱 로직
   - `luaK_xxx` 호출만 AST 노드 생성으로 대체
   - 토큰 소비 순서, enterlevel, 에러 처리 **모두 동일**
   
   **코드 생성 (lcodegen.c)**:
   - lcode.c와 **100% 동일**한 코드 생성 로직
   - expdesc → ExpInfo (이름만 다름, 구조 동일)
   - freereg 관리, 레지스터 할당 **모두 동일**
   - 입력이 expdesc 대신 AstNode인 것만 차이

**4. 실패한 접근들 (절대 하지 마라)**
   - ❌ `gen_expr(node, int target)` - target register 직접 전달
   - ❌ 매 함수마다 `saved_freereg = cg->freereg; ... cg->freereg = saved_freereg;`
   - ❌ expdesc/ExpInfo 없이 레지스터 번호만으로 관리
   - ❌ "더 간단한" 방법을 직접 발명
   - ❌ lparser/lcode의 "불필요해 보이는" 코드 생략

**5. 성공 공식**
   ```
   성공 = lparser.c를 옆에 두고 + 라인 단위로 읽고 + 정확히 변환
   ```

**6. 디버깅 시간 vs 참고 시간**
   - lparser/lcode 정확히 참고: **2-3일**
   - 대충 참고하고 디버깅: **2-3주** (그리고 실패 가능)
   
**7. 이 문서를 만든 이유**
   - 실패를 반복하지 않기 위해
   - lparser/lcode 참고의 중요성을 강조하기 위해
   - **"이번에는 다를 거야"라는 착각을 막기 위해**

---

### 구현 시작 전 체크리스트

구현을 시작하기 전에 이것들을 확인하라:

- [ ] lparser.c를 열어서 읽어봤는가?
- [ ] lcode.c를 열어서 읽어봤는가?
- [ ] expdesc 구조체를 이해했는가?
- [ ] luaK_exp2nextreg 함수를 읽어봤는가?
- [ ] "더 간단한 방법"을 시도하려는 유혹을 버렸는가?
- [ ] 이 문서의 최상단 경고를 읽었는가?

**하나라도 체크 안 되면 구현하지 마라. 실패한다.**