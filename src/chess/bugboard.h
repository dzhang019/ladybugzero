#pragma once
#include <string>
#include <cstdint>
#include "chess/board.h"

namespace lczero {

class BugBoard : public ChessBoard {
public:
  BugBoard() = default;
  BugBoard(const BugBoard&) = default;
  BugBoard(const std::string& fen) { SetFromFen(fen); }
  

  BugBoard& operator=(const BugBoard&) = default;


  // Loads an ordinary chess FEN with empty pockets and no promotion markers.
  void SetFromFen(std::string_view fen, int* moves = nullptr);
  // Parses a coordinate move or a drop such as N@f3 on this board.
  Move ParseMove(std::string_view move_str) const;
  bool SamePositionForRepetition(const BugBoard& other) const;
  // Nullifies the whole structure.
  void Clear() override;
  // Swaps black and white pieces and mirrors them relative to the
  // middle of the board. (what was on rank 1 appears on rank 8, what was
  // on file b remains on file b).
  void Mirror() override;

  // Generates list of possible moves for "ours" (white), but may leave king
  // under check.
  MoveList GeneratePseudolegalMoves() const override;
  
  // Applies the move. (Only for "ours" (white)). Returns a uint8_t
  // containing information about the move so the other bugboard can
  // update its pocket if capture occurs.
  // - 0 - pawn (or promoted piece) was captured
  // - 1 - bishop was captured
  // - 2 - rook was captured
  // - 3- queen was captured
  // - 4 - knight was captured 
  // - 255 - Nothing was captured
  uint8_t BugApplyMove(Move move);
  
  bool HasMatingMaterial() const override;
  void AddToPocket(uint8_t piece, bool our_pocket) {
    assert(piece < 5 && GetPocketCount(piece, our_pocket) < kPocketLimits[piece]);
    const int offset = kPocketOffsets[piece] + (our_pocket ? 0 : 32);
    pockets_ += uint64_t{1} << offset;
  }
  void RemoveFromPocket(uint8_t piece) {
    assert(piece < 5 && GetPocketCount(piece, true) > 0);
    pockets_ -= uint64_t{1} << kPocketOffsets[piece];
  }
  uint8_t GetPocketCount(uint8_t piece, bool our_pocket) const {
    const int offset = kPocketOffsets[piece] + (our_pocket ? 0 : 32);
    return static_cast<uint8_t>((pockets_ >> offset) & kPocketCountMasks[piece]);
  }
  // Check whether pseudolegal move is legal.
  bool IsLegalMove(Move move, const KingAttackInfo& king_attack_info) const override;

  uint64_t GetPockets() const { return pockets_; }

  //TODO: add pocket
  uint64_t Hash() const {
    return HashCat({our_pieces_.as_int(), their_pieces_.as_int(),
                    rooks_.as_int(), bishops_.as_int(), pawns_.as_int(),
                    (static_cast<uint32_t>(our_king_.as_idx()) << 24) |
                        (static_cast<uint32_t>(their_king_.as_idx()) << 16) |
                        (static_cast<uint32_t>(castlings_.as_int()) << 8) |
                        static_cast<uint32_t>(flipped_)});
  }

  BitBoard promoted() const { return promoted_pieces_; }

  bool operator==(const BugBoard& other) const = default;
  bool operator!=(const BugBoard& other) const = default;

 private:
  // Check internal state is consistent after state transformations.
  bool IsValid() const override;

  // All pieces that used to be pawns.
  BitBoard promoted_pieces_;
  // Pocket fields are ordered pawn, bishop, rook, queen, knight.
  // Each side can hold material originating on both boards.
  static constexpr int kPocketOffsets[] = {0, 6, 10, 14, 17};
  static constexpr uint8_t kPocketCountMasks[] = {63, 15, 15, 7, 15};
  static constexpr uint8_t kPocketLimits[] = {32, 8, 8, 4, 8};
  // Our pocket is in the lower 32 bits and theirs is in the upper 32 bits.
  uint64_t pockets_{0};
};

} // namespace lczero
