#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "chess/bugboard.h"
namespace lczero {

struct BugMove {
  Move move;
  bool is_board_b;
};

class BugPosition {
 public:
  BugPosition() = default;
  // From parent position and board-specific move. Need to communicate capture to other board.
  BugPosition(const BugPosition& parent, Move m, bool isBoardB);
  // From particular bug position.
  BugPosition(const BugBoard& boardA, const BugBoard& boardB, int plyCountA, int plyCountB);
  // Loads two ordinary chess FENs with empty bughouse pockets.
  static BugPosition FromFen(std::string_view fenA, std::string_view fenB);

  // Parses a board-prefixed move such as A:e2e4 or B:N@f3.
  BugMove ParseMove(std::string_view move_str) const;

  bool IsBlackToMove(bool isBoardB) const { return isBoardB ? us_board_B_.flipped() : us_board_A_.flipped(); }

  // Number of board-specific half-moves since beginning of the game.
  int GetBoardPly(bool isBoardB) const { return isBoardB ? ply_count_B_ : ply_count_A_; }
  int GetGamePly() const { return GetBoardPly(false) + GetBoardPly(true); }

  // How many time the same position appeared in the board before.
  int GetRepetitions(bool isBoardB) const { return isBoardB ? repetitions_B_ : repetitions_A_; }

  // How many half-moves since the same position appeared in the board before.
  int GetPliesSincePrevRepetition(bool isBoardB) const { return isBoardB ? cycle_length_B_ : cycle_length_A_; }

  // Someone outside that class knows better about repetitions, so they can
  // set it.
  void SetRepetitions(int repetitions, int cycle_length, bool isBoardB) {
    if (isBoardB) {
      repetitions_B_ = repetitions;
      cycle_length_B_ = cycle_length;
    } else{
      repetitions_A_ = repetitions;
      cycle_length_A_ = cycle_length;
    }
  }

  // Gets board from the point of view of player to move.
  const BugBoard& GetBoardA() const { return us_board_A_; }
  const BugBoard& GetBoardB() const { return us_board_B_; }

  bool operator==(const BugPosition&) const = default;
  bool operator!=(const BugPosition&) const = default;

  std::string DebugString() const;

 private:
  // The boards from the point of view of the player to move.
  BugBoard us_board_A_;
  BugBoard us_board_B_;

  // How many repetitions this position had before. For new positions it's 0.
  int repetitions_A_ = 0;
  int repetitions_B_ = 0;

  // How many half-moves since the position was repeated or 0.
  int cycle_length_A_ = 0;
  int cycle_length_B_ = 0;

  // number of half-moves since beginning of the game.
  int ply_count_A_ = 0;
  int ply_count_B_ = 0;
};

// These are ordered so max() prefers the best result.
enum class BoardResult : uint8_t { UNDECIDED, BLACK_WON, DRAW, WHITE_WON };
BoardResult operator-(const BoardResult& res);

// A_WON means White won on A or Black won on B. B_WON means White won on B or Black won on A.
enum class BugGameResult : uint8_t { UNDECIDED, A_WON, B_WON, DRAW };

class BugPositionHistory {
 public:
  BugPositionHistory() = default;
  BugPositionHistory(const BugPositionHistory& other) = default;
  BugPositionHistory(BugPositionHistory&& other) = default;
  BugPositionHistory(std::span<const BugPosition> positions)
      : positions_(positions.begin(), positions.end()) {}

  BugPositionHistory& operator=(const BugPositionHistory& other) = default;
  BugPositionHistory& operator=(BugPositionHistory&& other) = default;

  // Returns first position of the game (or fen from which it was initialized).
  const BugPosition& Starting() const { return positions_.front(); }

  // Returns the latest position of the game.
  const BugPosition& Last() const { return positions_.back(); }

  // N-th position of the game, 0-based.
  const BugPosition& GetPositionAt(int idx) const { return positions_[idx]; }

  // Trims position to a given size.
  void Trim(int size) {
    positions_.erase(positions_.begin() + size, positions_.end());
  }

  // Can be used to reduce allocation cost while performing a sequence of moves
  // in succession.
  void Reserve(int size) { positions_.reserve(size); }

  // Number of positions in history.
  int GetLength() const { return positions_.size(); }

  // Resets the position to a given state.
  void Reset(const BugBoard& boardA, const BugBoard& boardB, int boardAply, int boardBply);
  void Reset(const BugPosition& pos);

  // Appends a position to history.
  void Append(Move m, bool isBoardB);

  // Pops last move from history.
  void Pop() { positions_.pop_back(); }

  // Finds the endgame state (win/lose/draw/nothing) for a specific board on the last position.
  BoardResult ComputeBoardResult(bool isBoardB) const;

  // Find the endgame state(Awin/Bwin/draw/nothing) on the last position.
  BugGameResult ComputeGameResult() const;

  // Returns whether next move should be black's for a specific board.
  bool IsBlackToMove(bool isBoardB) const { return Last().IsBlackToMove(isBoardB); }

  std::span<const BugPosition> GetPositions() const { return positions_; }

 private:
  std::vector<BugPosition> positions_;
};

}  // namespace lczero
