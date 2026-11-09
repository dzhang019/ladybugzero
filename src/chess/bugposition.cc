
#include "chess/bugposition.h"

#include <limits>

#include "utils/exception.h"

namespace lczero {

BugPosition::BugPosition(const BugPosition& parent, Move m, bool isBoardB)
    : ply_count_A_(parent.ply_count_A_),
      ply_count_B_(parent.ply_count_B_) {
  if (isBoardB) {
    ++ply_count_B_;
  } else {
    ++ply_count_A_;
  }

  us_board_A_ = parent.us_board_A_;
  us_board_B_ = parent.us_board_B_;

  const uint8_t capture = isBoardB ? us_board_B_.BugApplyMove(m) : us_board_A_.BugApplyMove(m);
  if (capture != 0b11111111) {
    const bool capturer_is_black = parent.IsBlackToMove(isBoardB);
    const bool partner_is_to_move = capturer_is_black != parent.IsBlackToMove(!isBoardB);
    BugBoard& receiving_board = isBoardB ? us_board_A_ : us_board_B_;
    receiving_board.AddToPocket(capture, partner_is_to_move);
  }
  isBoardB ? us_board_B_.Mirror(): us_board_A_.Mirror();
}

BugPosition::BugPosition(const BugBoard& boardA, const BugBoard& boardB,
                         int plyCountA, int plyCountB)
    : us_board_A_(boardA),
      us_board_B_(boardB),
      ply_count_A_(plyCountA),
      ply_count_B_(plyCountB) {}

BugPosition BugPosition::FromFen(std::string_view fenA, std::string_view fenB) {
  auto load_board = [](std::string_view fen, BugBoard& board) {
    int fullmove;
    board.SetFromFen(fen, &fullmove);
    if (fullmove < 1 || fullmove - 1 > std::numeric_limits<int>::max() / 2) {
      throw Exception("FEN fullmove number is out of range");
    }
    return 2 * (fullmove - 1) + board.flipped();
  };
  BugPosition pos;
  pos.ply_count_A_ = load_board(fenA, pos.us_board_A_);
  pos.ply_count_B_ = load_board(fenB, pos.us_board_B_);
  return pos;
}

BugMove BugPosition::ParseMove(std::string_view move_str) const {
  if (move_str.size() < 3 || move_str[1] != ':' ||
      (move_str[0] != 'A' && move_str[0] != 'B')) {
    throw Exception("Expected A: or B: followed by a move");
  }
  const bool is_board_b = move_str[0] == 'B';
  const BugBoard& board = is_board_b ? GetBoardB() : GetBoardA();
  return {board.ParseMove(move_str.substr(2)), is_board_b};
}

std::string BugPosition::DebugString() const {
  return "Board A\n" + us_board_A_.DebugString() + "\nBoard B\n" +
         us_board_B_.DebugString();
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

BugGameResult BugPositionHistory::ComputeGameResult() const {
  BoardResult A = BugPositionHistory::ComputeBoardResult(false);
  BoardResult B = BugPositionHistory::ComputeBoardResult(true);
  if (A != BoardResult::UNDECIDED) {
    if (A == BoardResult::DRAW) return BugGameResult::DRAW;
    return A == BoardResult::WHITE_WON ? BugGameResult::A_WON
                                       : BugGameResult::B_WON;
  }
  if (B != BoardResult::UNDECIDED) {
    if (B == BoardResult::DRAW) return BugGameResult::DRAW;
    return B == BoardResult::BLACK_WON ? BugGameResult::A_WON
                                       : BugGameResult::B_WON;
  }
  return BugGameResult::UNDECIDED;
}

void BugPositionHistory::Reset(const BugBoard& boardA, const BugBoard& boardB,
                               int boardAply, int boardBply) {
  Reset(BugPosition(boardA, boardB, boardAply, boardBply));
}

void BugPositionHistory::Reset(const BugPosition& pos) {
  positions_ = {pos};
}

void BugPositionHistory::Append(Move m, bool isBoardB) {
  const BugPosition& parent = Last();
  BugPosition next(parent, m, isBoardB);
  const bool other_board_is_b = !isBoardB;
  next.SetRepetitions(parent.GetRepetitions(other_board_is_b),
                      parent.GetPliesSincePrevRepetition(other_board_is_b),
                      other_board_is_b);

  const BugBoard& board = isBoardB ? next.GetBoardB() : next.GetBoardA();
  const int ply = next.GetBoardPly(isBoardB);
  int last_checked_ply = ply;
  for (auto it = positions_.rbegin(); it != positions_.rend(); ++it) {
    const int previous_ply = it->GetBoardPly(isBoardB);
    if (previous_ply == last_checked_ply) continue;
    last_checked_ply = previous_ply;

    const BugBoard& previous =
        isBoardB ? it->GetBoardB() : it->GetBoardA();
    if (board.SamePositionForRepetition(previous)) {
      next.SetRepetitions(1 + it->GetRepetitions(isBoardB),
                          ply - previous_ply, isBoardB);
      break;
    }
  }
  positions_.push_back(next);
}

}  // namespace lczero
