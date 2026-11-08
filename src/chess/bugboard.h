#pragma once
#include <string>
#include <cstdint>
#include "chess/board.h"

namespace lczero {

//pbrqn
constexpr uint16_t kPocketMasks[10] = {
  0x001F, 0x00E0, 0x0700, 0x1800, 0xE000,
  0, 5, 8, 11, 13 
};

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
  //nqrbp
  // - 0 - pawn (or promoted piece) was captured
  // - 1 - bishop was captured
  // - 2 - rook was captured
  // - 3- queen was captured
  // - 4 - knight was captured 
  // - 255 - Nothing was captured
  uint8_t BugApplyMove(Move move);
  
  bool HasMatingMaterial() const override;
  //adds partner capture to my pocket
  void AddToPocket(uint8_t partner_captured) { pockets_ += 1 << kPocketMasks[partner_captured+5]; }
  void RemoveFromPocket(uint8_t piece_placed) { pockets_ -= 1 << kPocketMasks[piece_placed+5]; }
  static constexpr uint8_t GetPocketCount(uint8_t piece, uint16_t my_pocket) { return (my_pocket & kPocketMasks[piece]) << kPocketMasks[piece + 5]; }
  // Check whether pseudolegal move is legal.
  bool IsLegalMove(Move move, const KingAttackInfo& king_attack_info) const override;

  uint32_t GetPockets() const { return pockets_; }

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
  //nqrbp
  // - Bits 0-4 -- "our" side available pawns
  // - Bits 5-7 -- "our" side available bishops
  // - Bits 8-10 -- "our" side available rooks
  // - Bit 11-12 -- "our" side available queen
  // - Bit 13-15 -- "our" side available knights 
  // "their" side availability = "our" << 16 bits
  uint32_t pockets_{0};
};

} // namespace lczero
