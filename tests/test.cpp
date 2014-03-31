#define BOOST_TEST_MODULE test
#include <boost/test/included/unit_test.hpp>
#include <boost/algorithm/cxx11/all_of.hpp>
#include <boost/assign/list_of.hpp>

#include <algorithm>

#include "util.hpp"
#include "direction.hpp"
#include "state.hpp"
#include "moves.hpp"

BOOST_AUTO_TEST_CASE(knight_attacks_on_border) {
  BOOST_CHECK_EQUAL(moves::knight_attacks_ssw(squares::b1), 0);
}

BOOST_AUTO_TEST_CASE(initial_moves) {
  State state;

  std::set<Move> expected_moves;
  using namespace directions;
  bitboard::for_each_member(ranks::_2, [&expected_moves](squares::Index from) {
      expected_moves.emplace(from, from +   north, Move::Type::normal);
      expected_moves.emplace(from, from + 2*north, Move::Type::double_push);
    });
  bitboard::for_each_member(squares::b1 | squares::g1, [&expected_moves](squares::Index from) {
      expected_moves.emplace(from, from + 2*north + west, Move::Type::normal);
      expected_moves.emplace(from, from + 2*north + east, Move::Type::normal);
    });

  const std::vector<Move> moves = state.moves();
  const std::set<Move> actual_moves(moves.begin(), moves.end());

  std::set<Move> falsenegatives, falsepositives;
  std::set_difference(expected_moves.begin(), expected_moves.end(),
                      actual_moves.begin(), actual_moves.end(),
                      std::inserter(falsenegatives, falsenegatives.begin()));
  std::set_difference(actual_moves.begin(), actual_moves.end(),
                      expected_moves.begin(), expected_moves.end(),
                      std::inserter(falsepositives, falsepositives.begin()));
  BOOST_CHECK_MESSAGE(falsenegatives.empty(), "legal moves not generated: " << falsenegatives);
  BOOST_CHECK_MESSAGE(falsepositives.empty(), "illegal moves generated: " << falsepositives);
}

BOOST_AUTO_TEST_CASE(various_moves) {
  State state("r1b2rk1/pp1P1p1p/q1p2n2/2N2PpB/1NP2bP1/2R1B3/PP2Q2P/R3K3 w Q g6 0 1");

  using namespace squares;

  std::set<Move> expected_moves;

  // a1 rook
  for (BoardPartition::Part target: {b1, c1, d1})
    expected_moves.emplace(Move(a1.index, target.index, Move::Type::normal));

  // e1 king
  for (BoardPartition::Part target: {d1, f1})
    expected_moves.emplace(Move(e1.index, target.index, Move::Type::normal));
  expected_moves.emplace(Move(e1.index, c1.index, Move::Type::castle_kingside));

  // a2 pawn
  expected_moves.emplace(Move(a2.index, a3.index, Move::Type::normal));
  expected_moves.emplace(Move(a2.index, a4.index, Move::Type::double_push));

  // b2 pawn
  expected_moves.emplace(Move(b2.index, b3.index, Move::Type::normal));
  expected_moves.emplace(Move(b2.index, b4.index, Move::Type::double_push));

  // e2 queen
  for (BoardPartition::Part target: {f1, f2, g2, f3, d3, d2, c2, d1})
    expected_moves.emplace(Move(e2.index, target.index, Move::Type::normal));

  // h2 pawn
  expected_moves.emplace(Move(h2.index, h3.index, Move::Type::normal));
  expected_moves.emplace(Move(h2.index, h4.index, Move::Type::double_push));

  // c3 rook
  for (BoardPartition::Part target: {c2, c1, d3, b3, a3})
    expected_moves.emplace(Move(c3.index, target.index, Move::Type::normal));
  
  // e3 bishop
  for (BoardPartition::Part target: {f2, g1, d4, d2, c1})
    expected_moves.emplace(Move(e3.index, target.index, Move::Type::normal));
  expected_moves.emplace(Move(e3.index, f4.index, Move::Type::capture));

  // b4 knight
  for (BoardPartition::Part target: {a6, d5, d3, c2})
    expected_moves.emplace(Move(b4.index, target.index, Move::Type::normal));
  for (BoardPartition::Part target: {a6, c6})
    expected_moves.emplace(Move(b4.index, target.index, Move::Type::capture));

  // c4 pawn

  // g4 pawn

  // c5 knight
  for (BoardPartition::Part target: {e6, e4, d3, b3})
    expected_moves.emplace(Move(c5.index, target.index, Move::Type::normal));
  for (BoardPartition::Part target: {a6, b7})
    expected_moves.emplace(Move(c5.index, target.index, Move::Type::capture));

  // f5 pawn
  expected_moves.emplace(Move(f5.index, g6.index, Move::Type::capture));

  // h5 bishop
  expected_moves.emplace(Move(h5.index, g6.index, Move::Type::normal));
  expected_moves.emplace(Move(h5.index, f7.index, Move::Type::capture));

  // d7 pawn
  for (Move::Type type: {Move::Type::promotion_knight,
                         Move::Type::promotion_bishop,
                         Move::Type::promotion_rook,
                         Move::Type::promotion_queen}) {
    expected_moves.emplace(Move(d7.index, d8.index, type));
  }
  for (Move::Type type: {Move::Type::capturing_promotion_knight,
                         Move::Type::capturing_promotion_bishop,
                         Move::Type::capturing_promotion_rook,
                         Move::Type::capturing_promotion_queen}) {
    expected_moves.emplace(Move(d7.index, c8.index, type));
  }

  const std::vector<Move> moves = state.moves();
  const std::set<Move> actual_moves(moves.begin(), moves.end());

  std::set<Move> falsenegatives, falsepositives;
  std::set_difference(expected_moves.begin(), expected_moves.end(),
                      actual_moves.begin(), actual_moves.end(),
                      std::inserter(falsenegatives, falsenegatives.begin()));
  std::set_difference(actual_moves.begin(), actual_moves.end(),
                      expected_moves.begin(), expected_moves.end(),
                      std::inserter(falsepositives, falsepositives.begin()));
  BOOST_CHECK_MESSAGE(falsenegatives.empty(), "legal moves not generated: " << falsenegatives);
  BOOST_CHECK_MESSAGE(falsepositives.empty(), "illegal moves generated: " << falsepositives);
}

BOOST_AUTO_TEST_CASE(_moves) {
  State state;

  state.make_moves("e4 e5 Nf3 Nc6 Bc4 Bc5 b4 Bxb4 c3 Ba5 d4 exd4 0-0 d3 Qb3 Qf6"
                   "e5 Qg6 Re1 Nge7 Ba3 b5 Qxb5 Rb8 Qa4 Bb6 Nbd2 Bb7 Ne4 Qf5"
                   "Bxd3 Qh5 Nf6+ gxf6 18.exf6 Rg8 19.Rad1 Qxf3 20.Rxe7+ Nxe7"
                   "21.Qxd7+ Kxd7 22.Bf5+ Ke8 23.Bd7+ Kf8");

  BOOST_CHECK(true);
}