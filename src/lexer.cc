/* Generated by re2c 1.0.1 */
// Copyright 2011 Google Inc. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lexer.h"

#include <stdio.h>

#include "eval_env.h"
#include "util.h"

bool Lexer::Error(const string& message, string* err) {
  // Compute line/column.
  int line = 1;
  const char* line_start = input_.str_;
  for (const char* p = input_.str_; p < last_token_; ++p) {
    if (*p == '\n') {
      ++line;
      line_start = p + 1;
    }
  }
  int col = last_token_ ? (int)(last_token_ - line_start) : 0;

  char buf[1024];
  snprintf(buf, sizeof(buf), "%s:%d: ", filename_.AsString().c_str(), line);
  *err = buf;
  *err += message + "\n";

  // Add some context to the message.
  const int kTruncateColumn = 72;
  if (col > 0 && col < kTruncateColumn) {
    int len;
    bool truncated = true;
    for (len = 0; len < kTruncateColumn; ++len) {
      if (line_start[len] == 0 || line_start[len] == '\n') {
        truncated = false;
        break;
      }
    }
    *err += string(line_start, len);
    if (truncated)
      *err += "...";
    *err += "\n";
    *err += string(col, ' ');
    *err += "^ near here";
  }

  return false;
}

Lexer::Lexer(const char* input) {
  Start("input", input);
}

void Lexer::Start(StringPiece filename, StringPiece input) {
  filename_ = filename;
  input_ = input;
  ofs_ = input_.str_;
  last_token_ = NULL;
}

const char* Lexer::TokenName(Token t) {
  switch (t) {
  case ERROR:    return "lexing error";
  case BUILD:    return "'build'";
  case COLON:    return "':'";
  case DEFAULT:  return "'default'";
  case EQUALS:   return "'='";
  case IDENT:    return "identifier";
  case INCLUDE:  return "'include'";
  case INDENT:   return "indent";
  case NEWLINE:  return "newline";
  case PIPE2:    return "'||'";
  case PIPE:     return "'|'";
  case POOL:     return "'pool'";
  case RULE:     return "'rule'";
  case SUBNINJA: return "'subninja'";
  case TEOF:     return "eof";
  }
  return NULL;  // not reached
}

const char* Lexer::TokenErrorHint(Token expected) {
  switch (expected) {
  case COLON:
    return " ($ also escapes ':')";
  default:
    return "";
  }
}

string Lexer::DescribeLastError() {
  if (last_token_) {
    switch (last_token_[0]) {
    case '\t':
      return "tabs are not allowed, use spaces";
    }
  }
  return "lexing error";
}

void Lexer::UnreadToken() {
  ofs_ = last_token_;
}

Lexer::Token Lexer::ReadToken() {
  const char* p = ofs_;
  const char* q;
  const char* start;
  Lexer::Token token;
  for (;;) {
    start = p;
    
{
	unsigned char yych;
	unsigned int yyaccept = 0;
	static const unsigned char yybm[] = {
		  0, 128, 128, 128, 128, 128, 128, 128, 
		128, 128,   0, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		160, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 192, 192, 128, 
		192, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 128, 128, 128, 128, 128, 128, 
		128, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 128, 128, 128, 128, 192, 
		128, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 192, 192, 192, 192, 192, 
		192, 192, 192, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
	};
	yych = *p;
	if (yybm[0+yych] & 32) {
		goto yy9;
	}
	if (yych <= '^') {
		if (yych <= ',') {
			if (yych <= '\f') {
				if (yych <= 0x00) goto yy2;
				if (yych == '\n') goto yy6;
				goto yy4;
			} else {
				if (yych <= '\r') goto yy8;
				if (yych == '#') goto yy12;
				goto yy4;
			}
		} else {
			if (yych <= ':') {
				if (yych == '/') goto yy4;
				if (yych <= '9') goto yy13;
				goto yy16;
			} else {
				if (yych <= '=') {
					if (yych <= '<') goto yy4;
					goto yy18;
				} else {
					if (yych <= '@') goto yy4;
					if (yych <= 'Z') goto yy13;
					goto yy4;
				}
			}
		}
	} else {
		if (yych <= 'i') {
			if (yych <= 'b') {
				if (yych == '`') goto yy4;
				if (yych <= 'a') goto yy13;
				goto yy20;
			} else {
				if (yych == 'd') goto yy21;
				if (yych <= 'h') goto yy13;
				goto yy22;
			}
		} else {
			if (yych <= 'r') {
				if (yych == 'p') goto yy23;
				if (yych <= 'q') goto yy13;
				goto yy24;
			} else {
				if (yych <= 'z') {
					if (yych <= 's') goto yy25;
					goto yy13;
				} else {
					if (yych == '|') goto yy26;
					goto yy4;
				}
			}
		}
	}
yy2:
	++p;
	{ token = TEOF;     break; }
yy4:
	++p;
yy5:
	{ token = ERROR;    break; }
yy6:
	++p;
	{ token = NEWLINE;  break; }
yy8:
	yych = *++p;
	if (yych == '\n') goto yy28;
	goto yy5;
yy9:
	yyaccept = 0;
	yych = *(q = ++p);
	if (yybm[0+yych] & 32) {
		goto yy9;
	}
	if (yych <= '\f') {
		if (yych == '\n') goto yy6;
	} else {
		if (yych <= '\r') goto yy30;
		if (yych == '#') goto yy32;
	}
yy11:
	{ token = INDENT;   break; }
yy12:
	yyaccept = 1;
	yych = *(q = ++p);
	if (yych <= 0x00) goto yy5;
	goto yy33;
yy13:
	yych = *++p;
yy14:
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = IDENT;    break; }
yy16:
	++p;
	{ token = COLON;    break; }
yy18:
	++p;
	{ token = EQUALS;   break; }
yy20:
	yych = *++p;
	if (yych == 'u') goto yy36;
	goto yy14;
yy21:
	yych = *++p;
	if (yych == 'e') goto yy37;
	goto yy14;
yy22:
	yych = *++p;
	if (yych == 'n') goto yy38;
	goto yy14;
yy23:
	yych = *++p;
	if (yych == 'o') goto yy39;
	goto yy14;
yy24:
	yych = *++p;
	if (yych == 'u') goto yy40;
	goto yy14;
yy25:
	yych = *++p;
	if (yych == 'u') goto yy41;
	goto yy14;
yy26:
	yych = *++p;
	if (yych == '|') goto yy42;
	{ token = PIPE;     break; }
yy28:
	++p;
	{ token = NEWLINE;  break; }
yy30:
	yych = *++p;
	if (yych == '\n') goto yy28;
yy31:
	p = q;
	if (yyaccept == 0) {
		goto yy11;
	} else {
		goto yy5;
	}
yy32:
	yych = *++p;
yy33:
	if (yybm[0+yych] & 128) {
		goto yy32;
	}
	if (yych <= 0x00) goto yy31;
	++p;
	{ continue; }
yy36:
	yych = *++p;
	if (yych == 'i') goto yy44;
	goto yy14;
yy37:
	yych = *++p;
	if (yych == 'f') goto yy45;
	goto yy14;
yy38:
	yych = *++p;
	if (yych == 'c') goto yy46;
	goto yy14;
yy39:
	yych = *++p;
	if (yych == 'o') goto yy47;
	goto yy14;
yy40:
	yych = *++p;
	if (yych == 'l') goto yy48;
	goto yy14;
yy41:
	yych = *++p;
	if (yych == 'b') goto yy49;
	goto yy14;
yy42:
	++p;
	{ token = PIPE2;    break; }
yy44:
	yych = *++p;
	if (yych == 'l') goto yy50;
	goto yy14;
yy45:
	yych = *++p;
	if (yych == 'a') goto yy51;
	goto yy14;
yy46:
	yych = *++p;
	if (yych == 'l') goto yy52;
	goto yy14;
yy47:
	yych = *++p;
	if (yych == 'l') goto yy53;
	goto yy14;
yy48:
	yych = *++p;
	if (yych == 'e') goto yy55;
	goto yy14;
yy49:
	yych = *++p;
	if (yych == 'n') goto yy57;
	goto yy14;
yy50:
	yych = *++p;
	if (yych == 'd') goto yy58;
	goto yy14;
yy51:
	yych = *++p;
	if (yych == 'u') goto yy60;
	goto yy14;
yy52:
	yych = *++p;
	if (yych == 'u') goto yy61;
	goto yy14;
yy53:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = POOL;     break; }
yy55:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = RULE;     break; }
yy57:
	yych = *++p;
	if (yych == 'i') goto yy62;
	goto yy14;
yy58:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = BUILD;    break; }
yy60:
	yych = *++p;
	if (yych == 'l') goto yy63;
	goto yy14;
yy61:
	yych = *++p;
	if (yych == 'd') goto yy64;
	goto yy14;
yy62:
	yych = *++p;
	if (yych == 'n') goto yy65;
	goto yy14;
yy63:
	yych = *++p;
	if (yych == 't') goto yy66;
	goto yy14;
yy64:
	yych = *++p;
	if (yych == 'e') goto yy68;
	goto yy14;
yy65:
	yych = *++p;
	if (yych == 'j') goto yy70;
	goto yy14;
yy66:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = DEFAULT;  break; }
yy68:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = INCLUDE;  break; }
yy70:
	yych = *++p;
	if (yych != 'a') goto yy14;
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy13;
	}
	{ token = SUBNINJA; break; }
}

  }

  last_token_ = start;
  ofs_ = p;
  if (token != NEWLINE && token != TEOF)
    EatWhitespace();
  return token;
}

bool Lexer::PeekToken(Token token) {
  Token t = ReadToken();
  if (t == token)
    return true;
  UnreadToken();
  return false;
}

void Lexer::EatWhitespace() {
  const char* p = ofs_;
  const char* q;
  for (;;) {
    ofs_ = p;
    
{
	unsigned char yych;
	static const unsigned char yybm[] = {
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		128,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
	};
	yych = *p;
	if (yybm[0+yych] & 128) {
		goto yy79;
	}
	if (yych <= 0x00) goto yy75;
	if (yych == '$') goto yy82;
	goto yy77;
yy75:
	++p;
	{ break; }
yy77:
	++p;
yy78:
	{ break; }
yy79:
	yych = *++p;
	if (yybm[0+yych] & 128) {
		goto yy79;
	}
	{ continue; }
yy82:
	yych = *(q = ++p);
	if (yych == '\n') goto yy83;
	if (yych == '\r') goto yy85;
	goto yy78;
yy83:
	++p;
	{ continue; }
yy85:
	yych = *++p;
	if (yych == '\n') goto yy87;
	p = q;
	goto yy78;
yy87:
	++p;
	{ continue; }
}

  }
}

bool Lexer::ReadIdent(string* out) {
  const char* p = ofs_;
  const char* start;
  for (;;) {
    start = p;
    
{
	unsigned char yych;
	static const unsigned char yybm[] = {
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0, 128, 128,   0, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128,   0,   0,   0,   0,   0,   0, 
		  0, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128,   0,   0,   0,   0, 128, 
		  0, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128, 128, 128, 128, 128, 128, 
		128, 128, 128,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
		  0,   0,   0,   0,   0,   0,   0,   0, 
	};
	yych = *p;
	if (yybm[0+yych] & 128) {
		goto yy93;
	}
	++p;
	{
      last_token_ = start;
      return false;
    }
yy93:
	yych = *++p;
	if (yybm[0+yych] & 128) {
		goto yy93;
	}
	{
      out->assign(start, p - start);
      break;
    }
}

  }
  last_token_ = start;
  ofs_ = p;
  EatWhitespace();
  return true;
}

bool Lexer::ReadEvalString(EvalString* eval, bool path, string* err) {
  const char* p = ofs_;
  const char* q;
  const char* start;
  for (;;) {
    start = p;
    
{
	unsigned char yych;
	static const unsigned char yybm[] = {
		  0,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,   0,  16,  16,   0,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 32,  16,  16,  16,   0,  16,  16,  16, 
		 16,  16,  16,  16,  16, 208, 144,  16, 
		208, 208, 208, 208, 208, 208, 208, 208, 
		208, 208,   0,  16,  16,  16,  16,  16, 
		 16, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208,  16,  16,  16,  16, 208, 
		 16, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208, 208, 208, 208, 208, 208, 
		208, 208, 208,  16,   0,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
		 16,  16,  16,  16,  16,  16,  16,  16, 
	};
	yych = *p;
	if (yybm[0+yych] & 16) {
		goto yy100;
	}
	if (yych <= '\r') {
		if (yych <= 0x00) goto yy98;
		if (yych <= '\n') goto yy103;
		goto yy105;
	} else {
		if (yych <= ' ') goto yy103;
		if (yych <= '$') goto yy107;
		goto yy103;
	}
yy98:
	++p;
	{
      last_token_ = start;
      return Error("unexpected EOF", err);
    }
yy100:
	yych = *++p;
	if (yybm[0+yych] & 16) {
		goto yy100;
	}
	{
      eval->AddText(StringPiece(start, p - start));
      continue;
    }
yy103:
	++p;
	{
      if (path) {
        p = start;
        break;
      } else {
        if (*start == '\n')
          break;
        eval->AddText(StringPiece(start, 1));
        continue;
      }
    }
yy105:
	yych = *++p;
	if (yych == '\n') goto yy108;
	{
      last_token_ = start;
      return Error(DescribeLastError(), err);
    }
yy107:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy120;
	}
	if (yych <= ' ') {
		if (yych <= '\f') {
			if (yych == '\n') goto yy112;
			goto yy110;
		} else {
			if (yych <= '\r') goto yy115;
			if (yych <= 0x1F) goto yy110;
			goto yy116;
		}
	} else {
		if (yych <= '/') {
			if (yych == '$') goto yy118;
			goto yy110;
		} else {
			if (yych <= ':') goto yy123;
			if (yych <= '`') goto yy110;
			if (yych <= '{') goto yy125;
			goto yy110;
		}
	}
yy108:
	++p;
	{
      if (path)
        p = start;
      break;
    }
yy110:
	++p;
yy111:
	{
      last_token_ = start;
      return Error("bad $-escape (literal $ must be written as $$)", err);
    }
yy112:
	yych = *++p;
	if (yybm[0+yych] & 32) {
		goto yy112;
	}
	{
      continue;
    }
yy115:
	yych = *++p;
	if (yych == '\n') goto yy126;
	goto yy111;
yy116:
	++p;
	{
      eval->AddText(StringPiece(" ", 1));
      continue;
    }
yy118:
	++p;
	{
      eval->AddText(StringPiece("$", 1));
      continue;
    }
yy120:
	yych = *++p;
	if (yybm[0+yych] & 64) {
		goto yy120;
	}
	{
      eval->AddSpecial(StringPiece(start + 1, p - start - 1));
      continue;
    }
yy123:
	++p;
	{
      eval->AddText(StringPiece(":", 1));
      continue;
    }
yy125:
	yych = *(q = ++p);
	if (yybm[0+yych] & 128) {
		goto yy129;
	}
	goto yy111;
yy126:
	yych = *++p;
	if (yych == ' ') goto yy126;
	{
      continue;
    }
yy129:
	yych = *++p;
	if (yybm[0+yych] & 128) {
		goto yy129;
	}
	if (yych == '}') goto yy132;
	p = q;
	goto yy111;
yy132:
	++p;
	{
      eval->AddSpecial(StringPiece(start + 2, p - start - 3));
      continue;
    }
}

  }
  last_token_ = start;
  ofs_ = p;
  if (path)
    EatWhitespace();
  // Non-path strings end in newlines, so there's no whitespace to eat.
  return true;
}
