
#include "chess/bugposition.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "chess/types.h"

namespace lczero {

BugPosition::BugPosition(const BugPosition& parent, Move m, bool isBoardB) {
  if(isBoardB){ ply_count_B_ = parent.ply_count_B_ + 1; }
  else { ply_count_A_ = parent.ply_count_A_ + 1; } 

  us_board_A_ = parent.us_board_A_;
  us_board_B_ = parent.us_board_B_;

  const uint8_t capture = isBoardB ? us_board_B_.BugApplyMove(m) : us_board_A_.BugApplyMove(m);
  if (capture != 0b11111111) {
    isBoardB ? us_board_A_.AddToPocket(capture) : us_board_B_.AddToPocket(capture);
  }
  isBoardB ? us_board_B_.Mirror(): us_board_A_.Mirror();
}

BugPosition::BugPosition(const BugBoard& boardA, const BugBoard& boardB int plyCountA, int plyCountB) : repetitions_(0), ply_count_A_(plyCountA), ply_count_B_(plyCountB) {
  us_board_A_ = boardA;
  us_board_B_ = boardB;
}

//TODO:not sure if we need this for bughouse
BugPosition BugPosition::FromFen(std::string_view fenA, std::string_view fenB) {
  BugPosition pos;
  pos.us_board_A_.SetFromFen(std::string(fenA), &pos.rule50_ply_, &pos.ply_count_);
  return pos;
}

uint64_t Position::Hash() const {
  return HashCat({us_board_.Hash(), static_cast<unsigned long>(repetitions_)});
}

std::string Position::DebugString() const {
  std::string fen = PositionToFen(*this);
  std::replace(fen.begin(), fen.end(), ' ', '_');
  return "https://lc0.org/fen/" + fen;
}

BoardResult operator-(const BoardResult& res) {
  return res == BoardResult::BLACK_WON   ? BoardResult::WHITE_WON
         : res == BoardResult::WHITE_WON ? BoardResult::BLACK_WON
                                        : res;
}

BoardResult BugPositionHistory::ComputeBoardResult(bool isBoardB) const {
  const auto& board = isBoardB ? Last().GetBoardB() : Last().GetBoardA();
  auto legal_moves = board.GenerateLegalMoves();
  if (legal_moves.empty()) {
    if (board.IsUnderCheck()) {
      // Checkmate.
      return IsBlackToMove(isBoardB) ? BoardResult::WHITE_WON : BoardResult::BLACK_WON;
    }
    // Stalemate.
    return BoardResult::DRAW;
  }

  if (!board.HasMatingMaterial()) return BoardResult::DRAW;
  if (Last().GetRepetitions(isBoardB) >= 2) return BoardResult::DRAW;

  return BoardResult::UNDECIDED;
}

GameResult BugPositionHistory::ComputeGameResult() const {
  BoardResult A = BugPositionHistory::ComputeBoardResult(false);
  BoardResult B = BugPositionHistory::ComputeBoardResult(true);
  if (A != BoardResult::UNDECIDED) {
    if (A == BoardResult::DRAW) { return GameResult::DRAW; }
    return A == BoardResult::WHITE_WON ? GameResult::A_WON : GameResult::B_WON;
  }
  if (B != BoardResult::UNDECIDED) {
    if (B == BoardResult::DRAW) { return GameResult::DRAW; }
    return B == BoardResult::BLACK_WON ? GameResult::A_WON : GameResult::B_WON;
  }
  return GameResult::UNDECIDED;
}

void BugPositionHistory::Reset(const ChessBoard& board, int rule50_ply,
                            int game_ply) {
  positions_.clear();
  positions_.emplace_back(board, rule50_ply, game_ply);
}

void BugPositionHistory::Reset(const BugPosition& pos) {
  positions_.clear();
  positions_.push_back(pos);
}

void BugPositionHistory::Append(Move m, bool isBoardB) {
  // TODO(mooskagh) That should be emplace_back(Last(), m), but MSVS STL
  //                has a bug in implementation of emplace_back, when
  //                reallocation happens. (it also reallocates Last())
  positions_.push_back(BugPosition(Last(), m, isBoardB));
  int cycle_length;
  int repetitions = ComputeLastMoveRepetitions(&cycle_length);
  positions_.back().SetRepetitions(repetitions, cycle_length);
}

int PositionHistory::ComputeLastMoveRepetitions(int* cycle_length) const {
  *cycle_length = 0;
  const auto& last = positions_.back();
  // TODO(crem) implement hash/cache based solution.
  if (last.GetRule50Ply() < 4) return 0;

  for (int idx = positions_.size() - 5; idx >= 0; idx -= 2) {
    const auto& pos = positions_[idx];
    if (pos.GetBoard() == last.GetBoard()) {
      *cycle_length = positions_.size() - 1 - idx;
      return 1 + pos.GetRepetitions();
    }
    if (pos.GetRule50Ply() < 2) return 0;
  }
  return 0;
}

bool PositionHistory::DidRepeatSinceLastZeroingMove() const {
  for (auto iter = positions_.rbegin(), end = positions_.rend(); iter != end;
       ++iter) {
    if (iter->GetRepetitions() > 0) return true;
    if (iter->GetRule50Ply() == 0) return false;
  }
  return false;
}

uint64_t PositionHistory::HashLast(int positions) const {
  uint64_t hash = positions;
  for (auto iter = positions_.rbegin(), end = positions_.rend(); iter != end;
       ++iter) {
    if (!positions--) break;
    hash = HashCat(hash, iter->Hash());
  }
  return HashCat(hash, Last().GetRule50Ply());
}

std::string PositionToFen(const Position& pos) {
  std::string result = BoardToFen(pos.GetBoard());
  result += " " + std::to_string(pos.GetRule50Ply());
  result += " " + std::to_string(
                      (pos.GetGamePly() + (pos.IsBlackToMove() ? 1 : 2)) / 2);
  return result;
}
}  // namespace lczero
