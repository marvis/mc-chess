#include <string>
#include <cctype>

#include <boost/lexical_cast.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string_regex.hpp>
#include <boost/format.hpp>

#include "state.hpp"
#include "direction.hpp"

State::State() {
  using namespace colors;
  using namespace pieces;
  using namespace squares::bitboards;
  using namespace ranks::bitboards;

  board[white][pawn] = _2;
  board[white][knight] = b1 | g1;
  board[white][bishop] = c1 | f1;
  board[white][rook] = a1 | h1;
  board[white][queen] = d1;
  board[white][king] = e1;

  board[black][pawn] = _7;
  board[black][knight] = b8 | g8;
  board[black][bishop] = c8 | f8;
  board[black][rook] = a8 | h8;
  board[black][queen] = d8;
  board[black][king] = e8;

  for (Color color: colors::values)
    for (Castle castle: castles::values)
      castling_rights[color][castle] = true;

  en_passant_square = 0;

  us = white; them = black;

  compute_occupancy();
  compute_their_attacks();
  compute_hash();
}

State::State(std::string fen)
{
  boost::regex fen_regex("((\\w+/){7}\\w+)\\s+([bw])\\s+((K)?(Q)?(k)?(q)?|-)\\s+(([a-h][1-8])|-)\\s+.*");
  boost::smatch m;
  if (!boost::regex_match(fen, m, fen_regex))
    throw std::runtime_error(str(boost::format("can't parse FEN: %1%") % fen));

  std::string board(m[1].first, m[1].second);

  boost::regex rank_separator("/");
  std::vector<std::string> fen_ranks;
  boost::algorithm::split_regex(fen_ranks, std::string(m[1].first, m[1].second), rank_separator);
  std::reverse(fen_ranks.begin(), fen_ranks.end());

  using namespace pieces;
  empty_board();
  squares::Index square = squares::a1;
  for (ranks::Index rank: ranks::indices) {
    for (char c: fen_ranks[rank]) {
      if (isdigit(c)) {
        short n = boost::lexical_cast<short>(c);
        assert(1 <= n && n <= 8);
        square = static_cast<squares::Index>(square + n);
      } else {
        Color owner = islower(c) ? colors::black : colors::white;
        c = tolower(c);
        Bitboard board = squares::bitboard(square);
        switch (c) {
        case 'p': this->board[owner][pawn]   |= board; break;
        case 'n': this->board[owner][knight] |= board; break;
        case 'b': this->board[owner][bishop] |= board; break;
        case 'r': this->board[owner][rook]   |= board; break;
        case 'q': this->board[owner][queen]  |= board; break;
        case 'k': this->board[owner][king]   |= board; break;
        default: throw std::runtime_error(str(boost::format("invalid FEN piece symbol %1% in FEN \"%2%\"") % c % fen));
        }
        square = static_cast<squares::Index>(square + 1);
      }
    }

    if (square != (rank + 1) * 8)
      throw std::runtime_error(str(boost::format("rank %1% has width unequal to 8 in FEN \"%2%\"") % ranks::keywords[rank] % fen));
  }

  Color color_to_move = std::string(m[3].first, m[3].second) == "w" ? colors::white : colors::black;

  castling_rights[colors::white][castles::kingside]  = m[5].matched;
  castling_rights[colors::white][castles::queenside] = m[6].matched;
  castling_rights[colors::black][castles::kingside]  = m[7].matched;
  castling_rights[colors::black][castles::queenside] = m[8].matched;

  if (m[10].matched) {
    en_passant_square = squares::bitboard(squares::by_keyword(std::string(m[10].first, m[10].second)));
  } else {
    en_passant_square = 0;
  }

  us = color_to_move;
  them = us == colors::white ? colors::black : colors::white;

  compute_occupancy();
  compute_their_attacks();
  compute_hash();
}

State::State(State &that) :
  us(that.us),
  them(that.them),
  board(that.board),
  castling_rights(that.castling_rights),
  en_passant_square(that.en_passant_square),
  occupancy(that.occupancy),
  their_attacks(that.their_attacks),
  hash(that.hash)
{
}

State::~State() {}

void State::empty_board() {
  for (Color c: colors::values)
    for (Piece p: pieces::values)
      board[c][p] = 0;
}

bool State::operator==(const State &that) const {
  if (this == &that)
    return true;
  
  return this->hash == that.hash &&
         this->us == that.us && this->them == that.them &&
         this->en_passant_square == that.en_passant_square &&
         this->castling_rights == that.castling_rights &&
         this->board == that.board;
}

std::ostream& operator<<(std::ostream& o, const State& s) {
  array2d<char, colors::cardinality, pieces::cardinality> symbols = {
    'P', 'N', 'B', 'R', 'Q', 'K',
    'p', 'n', 'b', 'r', 'q', 'k',
  };

  Board board(s.board);
  // squares run from white to black, output runs from top to bottom.  flip
  // the board to get white on the bottom.
  board::flip_vertically(board);

  for (squares::Index square: squares::indices) {
    o << ' ';

    bool piece_found = false;
    for (Color c: colors::values) {
      for (Piece p: pieces::values) {
        if (board[c][p] & squares::bitboard(square)) {
          o << symbols[c][p];

          // take this opportunity to ensure no two pieces are on the same square
          assert(!piece_found);
          piece_found = true;
        }
      }
    }

    if (!piece_found)
      o << '.';

    if ((square + 1) % 8 == 0)
      o << std::endl;
  }

  o << colors::name(s.us) << " to move.";

  o << " castling rights: ";
  for (Color color: colors::values) {
    for (Castle castle: castles::values) {
      if (s.castling_rights[color][castle])
        o << castles::symbol(color, castle);
    }
  }

  if (s.en_passant_square != 0)
    o << " en-passant square: " << squares::keywords[squares::index(s.en_passant_square)];

  o << std::endl;
  return o;
}

bool State::leaves_king_in_check(const Move& move) const {
  const Bitboard source = squares::bitboard(move.source()),
                 target = squares::bitboard(move.target());

  Bitboard king = board[us][pieces::king];

  // king move?
  if (source & king)
    return target & their_attacks;

  // only occupancy and their halfboard make a difference
  Occupancy occupancy(this->occupancy);
  Halfboard their_halfboard(board[them]);

  Hash hash; // we don't care about what happens to this
  Piece piece = moving_piece(move, board[us]);
  make_move_on_occupancy(move, piece, source, target, occupancy, hash);
  make_move_on_their_halfboard(move, piece, source, target, their_halfboard, hash);

  Bitboard flat_occupancy;
  board::flatten(occupancy, flat_occupancy);
  return moves::attacks(them, flat_occupancy, their_halfboard) & king;
}

std::vector<Move> State::moves() const {
  // TODO: maybe reserve()
  std::vector<Move> moves;
  moves::moves(moves, us, them,
               board, occupancy,
               their_attacks, en_passant_square,
               castling_rights);

  // filter out moves that would leave the king in check
  for (auto it = moves.begin(); it != moves.end(); ) {
    if (leaves_king_in_check(*it)) {
      it = moves.erase(it);
    } else {
      it++;
    }
  }

  return moves;
}

boost::optional<Move> State::random_move(boost::mt19937& generator) const {
  // TODO: do better
  std::vector<Move> moves = this->moves();
  if (moves.empty())
    return boost::none;
  boost::uniform_int<> distribution(0, moves.size() - 1);
  Move move = moves.at(distribution(generator));
  return move;
}

Move State::parse_algebraic(std::string algebraic) const {
  boost::regex algebraic_move_regex("([NBRQK]?)([a-h])?([1-8])?(x)?([a-h][1-8])\\+?|(O-O-O|0-0-0)|(O-O|0-0)");
  boost::smatch m;
  if (!boost::regex_match(algebraic, m, algebraic_move_regex))
    throw std::runtime_error(str(boost::format("can't parse algebraic move: %1%") % algebraic));

  std::function<bool(const Move&)> predicate;
  if (m[6].matched) {
    predicate = [this](const Move& move) {
      return move == Move::castle(us, castles::queenside);
    };
  } else if (m[7].matched) {
    predicate = [this](const Move& move) {
      return move == Move::castle(us, castles::kingside);
    };
  } else {
    Piece piece = pieces::type_from_name(std::string(m[1].first, m[1].second));

    boost::optional<files::Index> source_file;
    boost::optional<ranks::Index> source_rank;
    if (m[2].matched) source_file = files::by_keyword(std::string(m[2].first, m[2].second));
    if (m[3].matched) source_rank = ranks::by_keyword(std::string(m[3].first, m[3].second));

    bool is_capture = m[4].matched;
    squares::Index target = squares::by_keyword(std::string(m[5].first, m[5].second));

    predicate = [this, piece, source_file, source_rank, is_capture, target](const Move& move) {
      if (!(board[us][piece] & squares::bitboard(move.from())))
        return false;
      if (!move.matches_algebraic(source_file, source_rank, target, is_capture))
        return false;
      return true;
    };
  }

  std::vector<Move> candidates = moves();

  for (auto it = candidates.begin(); it != candidates.end(); ) {
    if (!predicate(*it)) {
      it = candidates.erase(it);
    } else {
      it++;
    }
  }

  if (candidates.empty())
    throw std::runtime_error(str(boost::format("no match for algebraic move: %1%") % algebraic));
  if (candidates.size() > 1)
    throw std::runtime_error(str(boost::format("ambiguous algebraic move: %1%, candidates: %2%") % algebraic % candidates));
  return candidates[0];
}

void State::make_moves(std::string algebraic_variation) {
  boost::regex algebraic_separator("\\s+(\\d+\\.)?");
  std::vector<std::string> algebraic_moves;
  boost::algorithm::split_regex(algebraic_moves,
                                boost::algorithm::trim_copy(algebraic_variation),
                                algebraic_separator);
  make_moves(algebraic_moves);
}

void State::make_moves(std::vector<std::string> algebraic_moves) {
  for (std::string algebraic_move: algebraic_moves) {
    make_move(parse_algebraic(algebraic_move));
  }
}

Piece State::moving_piece(const Move& move, const Halfboard& us) const {
  const Bitboard source = squares::bitboard(move.from());
  for (Piece piece: pieces::values) {
    if (us[piece] & source)
      return piece;
  }
  std::cerr << "State::moving_piece: no match for move " << move << " in state: " << std::endl;
  std::cerr << *this << std::endl;
  print_backtrace();
  assert(false);
}

void State::update_castling_rights(const Move& move, const Piece piece, const Bitboard source, const Bitboard target,
                                   CastlingRights& castling_rights,
                                   Hash& hash) const {
  switch (piece) {
  case pieces::king:
    for (Castle castle: castles::values) {
      if (castling_rights[us][castle]) {
        castling_rights[us][castle] = false;
        hash ^= hashes::can_castle(us, castle);
      }
    }
    break;
  case pieces::rook:
    {
      boost::optional<Castle> castle = castles::involving(move.source(), us);
      if (castle && castling_rights[us][*castle]) {
        castling_rights[us][*castle] = false;
        hash ^= hashes::can_castle(us, *castle);
      }
    }
    break;
  case pieces::pawn:
  case pieces::knight:
  case pieces::bishop:
  case pieces::queen:
    break;
  default:
    throw std::runtime_error("unhandled piece");
  }
}

void State::update_en_passant_square(const Move& move, const Piece piece, const Bitboard source, const Bitboard target,
                                     Bitboard& en_passant_square,
                                     Hash& hash) const {
  if (en_passant_square)
    hash ^= hashes::en_passant(en_passant_square);

  switch (move.type()) {
  case Move::Type::double_push:
    assert(piece == pieces::pawn);
    en_passant_square = us == colors::white ? target >> directions::vertical
                                            : target << directions::vertical;
    hash ^= hashes::en_passant(en_passant_square);
    break;
  case Move::Type::capture:
  case Move::Type::castle_kingside:
  case Move::Type::castle_queenside:
  case Move::Type::promotion_knight:
  case Move::Type::promotion_bishop:
  case Move::Type::promotion_rook:
  case Move::Type::promotion_queen:
  case Move::Type::normal:
    en_passant_square = 0;
    break;
  default:
    throw std::runtime_error("unhandled Move::Type case");
  }
}

void State::make_move_on_their_halfboard(const Move& move, const Piece piece, const Bitboard source, const Bitboard target,
                                         Halfboard& their_halfboard,
                                         Hash& hash) const {
  using hashes::toggle;

  switch (move.type()) {
  case Move::Type::capturing_promotion_knight:
  case Move::Type::capturing_promotion_bishop:
  case Move::Type::capturing_promotion_rook:
  case Move::Type::capturing_promotion_queen:
  case Move::Type::capture:
    if (target == en_passant_square) {
      assert(piece == pieces::pawn);
      // the captured pawn is in front of the en_passant_square
      Bitboard capture_target = us == colors::white ? target >> directions::vertical
                                                    : target << directions::vertical;
      assert(their_halfboard[pieces::pawn] & capture_target);

      their_halfboard[pieces::pawn] &= ~capture_target;
      toggle(hash, them, pieces::pawn, squares::index(capture_target));
    } else {
      for (Piece capturee: pieces::values) {
        if (their_halfboard[capturee] & target) {
          their_halfboard[capturee] &= ~target;
          toggle(hash, them, capturee, squares::index(target));
          break;
        }
      }
    }
    break;
  case Move::Type::double_push:
  case Move::Type::castle_kingside:
  case Move::Type::castle_queenside:
  case Move::Type::promotion_knight:
  case Move::Type::promotion_bishop:
  case Move::Type::promotion_rook:
  case Move::Type::promotion_queen:
  case Move::Type::normal:
    break;
  default:
    throw std::runtime_error("unhandled Move::Type case");
  }
}

void State::make_move_on_our_halfboard(const Move& move, const Piece piece, const Bitboard source, const Bitboard target,
                                       Halfboard& our_halfboard,
                                       Hash& hash) const {
  using namespace pieces;
  using hashes::toggle;

  our_halfboard[piece] &= ~source; toggle(hash, us, piece, move.source());
  our_halfboard[piece] |=  target; toggle(hash, us, piece, move.target());

  switch (move.type()) {
  case Move::Type::castle_kingside:
  case Move::Type::castle_queenside:
    {
      squares::Index rook0 = castles::rook_before(move.to());
      squares::Index rook1 = castles::rook_after(move.to());
      our_halfboard[rook] &= ~squares::bitboard(rook0); toggle(hash, us, rook, rook0);
      our_halfboard[rook] |=  squares::bitboard(rook1); toggle(hash, us, rook, rook1);
    }
    break;
  case Move::Type::capturing_promotion_knight:
  case Move::Type::promotion_knight:
    assert(piece == pawn);
    our_halfboard[piece]  &= ~target; toggle(hash, us, pawn,   move.target());
    our_halfboard[knight] |=  target; toggle(hash, us, knight, move.target());
    break;
  case Move::Type::capturing_promotion_bishop:
  case Move::Type::promotion_bishop:
    assert(piece == pawn);
    our_halfboard[piece]  &= ~target; toggle(hash, us, pawn,   move.target());
    our_halfboard[bishop] |=  target; toggle(hash, us, bishop, move.target());
    break;
  case Move::Type::capturing_promotion_rook:
  case Move::Type::promotion_rook:
    assert(piece == pawn);
    our_halfboard[piece] &= ~target; toggle(hash, us, pawn,  move.target());
    our_halfboard[rook]  |=  target; toggle(hash, us, rook,  move.target());
    break;
  case Move::Type::capturing_promotion_queen:
  case Move::Type::promotion_queen:
    assert(piece == pawn);
    our_halfboard[piece] &= ~target; toggle(hash, us, pawn,  move.target());
    our_halfboard[queen] |=  target; toggle(hash, us, queen, move.target());
    break;
  case Move::Type::capture:
  case Move::Type::double_push:
  case Move::Type::normal:
    break;
  default:
    throw std::runtime_error("unhandled Move::Type case");
  }
}

void State::make_move_on_occupancy(const Move& move, const Piece piece, const Bitboard source, const Bitboard target,
                                   Occupancy& occupancy,
                                   Hash& hash) const {
  occupancy[us] &= ~source;
  occupancy[us] |=  target;

  switch (move.type()) {
  case Move::Type::capturing_promotion_knight:
  case Move::Type::capturing_promotion_bishop:
  case Move::Type::capturing_promotion_rook:
  case Move::Type::capturing_promotion_queen:
  case Move::Type::capture:
    if (target == en_passant_square)
      occupancy[them] &= ~(target >> directions::vertical);
    else
      occupancy[them] &= ~target;
    break;
  case Move::Type::castle_kingside:
  case Move::Type::castle_queenside:
    occupancy[us] &= ~squares::bitboard(castles::rook_before(move.to()));
    occupancy[us] |=  squares::bitboard(castles::rook_after(move.to()));
    break;
  case Move::Type::double_push:
  case Move::Type::promotion_knight:
  case Move::Type::promotion_bishop:
  case Move::Type::promotion_rook:
  case Move::Type::promotion_queen:
  case Move::Type::normal:
    break;
  default:
    throw std::runtime_error("unhandled Move::Type case");
  }
}

void State::make_move(const Move& move) {
  Bitboard source = squares::bitboard(move.source()),
           target = squares::bitboard(move.target());
  Halfboard& our_halfboard = board[us];
  Piece piece = moving_piece(move, our_halfboard);

  update_castling_rights       (move, piece, source, target, castling_rights, hash);
  update_en_passant_square     (move, piece, source, target, en_passant_square, hash);
  make_move_on_our_halfboard   (move, piece, source, target, board[us], hash);
  make_move_on_their_halfboard (move, piece, source, target, board[them], hash);
  make_move_on_occupancy       (move, piece, source, target, occupancy, hash);

  std::swap(us, them);
  hash ^= hashes::black_to_move();

  compute_their_attacks();
}

void State::compute_occupancy()     { compute_occupancy(occupancy); }
void State::compute_their_attacks() { compute_their_attacks(their_attacks); }
void State::compute_hash()          { compute_hash(hash); }

void State::compute_occupancy(Occupancy& occupancy) {
  board::flatten(board, occupancy);
}

void State::compute_their_attacks(Bitboard& their_attacks) {
  Bitboard flat_occupancy;
  board::flatten(occupancy, flat_occupancy);
  their_attacks = moves::attacks(them, flat_occupancy, board[them]);
}

void State::compute_hash(Hash &hash) {
  hash = 0;

  for (Color c: colors::values) {
    for (Piece p: pieces::values) {
      squares::do_bits(board[c][p], [this, &c, &p, &hash](squares::Index si) {
          hash ^= hashes::colored_piece_at_square(c, p, si);
        });
    }

    for (Castle castle: castles::values)
      if (castling_rights[c][castle])
        hash ^= hashes::can_castle(c, castle);
  }

  if (us == colors::black)
    hash ^= hashes::black_to_move();

  if (en_passant_square)
    hash ^= hashes::en_passant(en_passant_square);
}
