# 단계 수정
"lexing -> (token) -> parsing -> (bytecode) -> vm"에서
"lexing -> (token) -> parsing -> (AST) -> code_gen -> (bytecode) -> vm"으로 단계 수정

```
AST* ast = make_ast();
Code code = parse(ast);
return code;
```

## last.*

+ 새로운 단계에서의 parsing(즉, AST 생성)을 담당하는 last.\* 파일을 만들어서 parsing을 구현할 것.

+ syntax error 보고, enter level, 기타 등등 문법에 관한 것은 모두 last.*에서 처리할 것.

+ interface는 다음을 참고하여 작성할 것.
    ```c
    AST *luaA_make(lua_State *L, ZIO *z, Mbuffer *buff, int firstchar);
    void luaA_free(lua_State *L, AST *ast);
    ```

### data structure

+ 루트 데이터 구조를 여기서는 AST라 부르겠다. 이름은 알맞게 지을 것.

+ LexState에서 lexing(tokenizing)에 필요 없는 필드들은 다 AST로 옮겨 올 것.
    즉, AST가 새로운 lparser.\*에서 LexState의 역할을 대체할 것.

+ 모든 expression과 statement 노드가 다음과 비슷하게 디버깅 정보를 가질 것.
    ```c
    typedef struct NodeInfo { // node header?
        int line;      // 이 노드가 시작하는 소스 라인
    // ...
    } NodeInfo;
    ```

+ 한 노드 타입에 모든 단말/비단말 구조를 넣지 말고 BNF에 맞게 설계할 것.

## lparser.\*

+ 기존 단계에서 parsing(즉, bytecode 생성)은 lparser가 담당하고 있음

+ 새로운 단계에서 lparser.\*는 그대로 bytecode 생성(즉, code_gen)을 담당.

+ lparser.\*는 llex.\*에 전혀 의존할 필요가 없다.

+ lparser 수정은 사용자가 할 것이므로, 이에 대한 가이드를 자세히 제공할 것.