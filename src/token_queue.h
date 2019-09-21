#include <list>

#include "token.h"

namespace zion {

struct TokenQueue {
  std::list<Token> m_queue;
  TokenKind m_last_tk = tk_none;
  void enqueue(const Location &location,
               TokenKind tk,
               const ZionString &token_text);
  void enqueue(const Location &location, TokenKind tk);
  bool empty() const;
  TokenKind last_tk() const;
  void set_last_tk(TokenKind tk);
  Token pop();
};

} // namespace zion
