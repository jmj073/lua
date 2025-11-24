# 단계 수정

"lexing -> (token) -> parsing -> (bytecode) -> vm"에서
"lexing -> (token) -> parsing -> (AST) -> code_gen -> (bytecode) -> vm"으로 단계 수정


## last.\*

+ 새로운 단계에서의 parsing(즉, AST 생성)을 담당하는 last.\* 파일을 만들어서 parsing을 구현할 것.

+ syntax error 보고, enter level, 기타 등등 문법에 관한 것은 모두 last.\*에서 처리할 것. 이는 lparser.\*를 참고

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

+ lparser.\*를 참고하여 lcodegen.\*을 작성할 것.

+ lcodegen.\*은 AST를 받아 bytecode를 생성.

+ Dyndata는 lcodegen에서만 사용 (last에서는 사용 안 함)

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