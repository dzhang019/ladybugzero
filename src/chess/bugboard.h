#pragma once
#include <string>
#include <cstdint>
#include "chess/board.h"

namespace lczero {

//pbrqn
constexpr uint16_t kPocketMasks[10] = {
  0x000F, 0x0030, 0x00C0, 0x0100, 0x0600,
  0, 4, 6, 7, 9 
};

class BugBoard : public ChessBoard {
public:
  BugBoard() = default;
  BugBoard(const BugBoard&) = default;
  BugBoard(const std::string& fen) { SetFromFen(fen); }
  

  BugBoard& operator=(const BugBoard&) = default;


  // Sets position from FEN string.
  // If @rule50_ply and @moves are not nullptr, they are filled with number
  // of moves without capture and number of full moves since the beginning of
  // the game.
  void SetFromFen(std::string_view fen,
                  int* moves = nullptr);
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
  // - 63 - Nothing was captured
  uint8_t BugApplyMove(Move move);
  
  bool HasMatingMaterial() const override;
  //adds partner capture to my pocket
  void AddToPocket(uint8_t partner_captured) { pockets_ += 1 << kPocketMasks[partner_captured+5]; }
  void RemoveFromPocket(uint8_t piece_placed) { pockets_ -= 1 << kPocketMasks[piece_placed+5]; }
  
  // Check whether pseudolegal move is legal.
  bool IsLegalMove(Move move, const KingAttackInfo& king_attack_info) const override;

  // Parses a move from move_str.
  // The input string should be in the "normal" notation rather than from the
  // player to move, i.e. "e7e5" for the black pawn move.
  // Output is currently "from the player to move" perspective (i.e. from=E2,
  // to=E4 for the same black move). This is temporary, plan is to change it
  // soon.
  Move ParseMove(std::string_view move_str) const;
  
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
  // - Bits 0-3 -- "our" side available pawns
  // - Bits 4-5 -- "our" side available bishops
  // - Bits 6-7 -- "our" side available rooks
  // - Bit 8 -- "our" side available queen
  // - Bit 9-10 -- "our" side available knights 
  // "their" side availability = "our" << 16 bits
  uint32_t pockets_;
};

} // namespace lczero