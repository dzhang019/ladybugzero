#include "chess/bugboard.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>
#include <absl/cleanup/cleanup.h>

#include "utils/exception.h"

#if not defined(NO_PEXT)
// Include header for pext instruction.
#include <immintrin.h>
#endif

namespace lczero {
static const BitBoard kPawnAttacks[] = {
    0x0000000000000200ULL, 0x0000000000000500ULL, 0x0000000000000A00ULL,
    0x0000000000001400ULL, 0x0000000000002800ULL, 0x0000000000005000ULL,
    0x000000000000A000ULL, 0x0000000000004000ULL, 0x0000000000020000ULL,
    0x0000000000050000ULL, 0x00000000000A0000ULL, 0x0000000000140000ULL,
    0x0000000000280000ULL, 0x0000000000500000ULL, 0x0000000000A00000ULL,
    0x0000000000400000ULL, 0x0000000002000000ULL, 0x0000000005000000ULL,
    0x000000000A000000ULL, 0x0000000014000000ULL, 0x0000000028000000ULL,
    0x0000000050000000ULL, 0x00000000A0000000ULL, 0x0000000040000000ULL,
    0x0000000200000000ULL, 0x0000000500000000ULL, 0x0000000A00000000ULL,
    0x0000001400000000ULL, 0x0000002800000000ULL, 0x0000005000000000ULL,
    0x000000A000000000ULL, 0x0000004000000000ULL, 0x0000020000000000ULL,
    0x0000050000000000ULL, 0x00000A0000000000ULL, 0x0000140000000000ULL,
    0x0000280000000000ULL, 0x0000500000000000ULL, 0x0000A00000000000ULL,
    0x0000400000000000ULL, 0x0002000000000000ULL, 0x0005000000000000ULL,
    0x000A000000000000ULL, 0x0014000000000000ULL, 0x0028000000000000ULL,
    0x0050000000000000ULL, 0x00A0000000000000ULL, 0x0040000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL,
    0x0000000000000000ULL};

void BugBoard::Clear() { *this = BugBoard(); }

void BugBoard::Mirror() {
  ChessBoard::Mirror();
  promoted_pieces_.Mirror();
  pockets_ = pockets_ & 0xFFFF0000 >> 16 | pockets_ & 0x0000FFFF << 16;
}

MoveList BugBoard::GeneratePseudolegalMoves() const {
  MoveList result;
  result.reserve(60);
  result = ChessBoard::GeneratePseudolegalMoves();
  uint16_t our_pocket = BugBoard::GetPockets() & 0x0000FFFF;
  BitBoard open = ~(our_pieces_ | their_pieces_) - BitBoard(0xFF000000000000FF);
  for (auto to : open){
    // - Bits 0-3 -- "our" side available pawns
    // - Bits 4-5 -- "our" side available bishops
    // - Bits 6-7 -- "our" side available rooks
    // - Bit 8 -- "our" side available queen
    // - Bit 9-10 -- "our" side available knights 
    for (int i=0;i<5;i++) {
      if (our_pocket & kPocketMasks[i]) {
        result.emplace_back(Move::Bug(Square::FromIdx(i), to));
      }
    }
  }
  return result;
}
// TODO: fix add knights()

bool BugBoard::IsValid() const {
  const auto all = ours() | theirs();
  auto check = all | pawns() | bishops() | rooks() | queens() | kings();
  if (check != all ||
      (pawns() & bishops()).as_int() ||
      (pawns() & rooks()).as_int() ||
      (pawns() & queens()).as_int() ||
      (pawns() & kings()).as_int() ||
      (bishops() & rooks()).as_int() ||
      (bishops() & queens()).as_int() ||
      (bishops() & kings()).as_int() ||
      (rooks() & queens()).as_int() ||
      (rooks() & kings()).as_int() ||
      (queens() & kings()).as_int() ||
      (~all & promoted()).as_int() ||
      (BugBoard::GetPockets() & 0x000F > 8) || 
      ((BugBoard::GetPockets() >> 16) & 0x000F > 8) ||
      (BugBoard::GetPockets() & 0x0030 > 2) ||
      ((BugBoard::GetPockets() >> 16) & 0x0030 > 2) ||
      (BugBoard::GetPockets() & 0x00C0 > 2) ||
      ((BugBoard::GetPockets() >> 16) & 0x00C0 > 2) ||
      (BugBoard::GetPockets() & 0x0100 > 1) ||
      ((BugBoard::GetPockets() >> 16) & 0x0100 > 1) ||
      (BugBoard::GetPockets() & 0x0600 > 2) ||
      ((BugBoard::GetPockets() >> 16) & 0x0600 > 2)) {
    return false;
  }
  return true;
}

uint8_t BugBoard::BugApplyMove(Move move) {
  // Bughouse pocket to board
  if (move.isBugMove()) {
    assert(static_cast<uint16_t>(pockets_) & kPocketMasks[move.from().as_idx()]);
    const Square to = move.to();
    const int piece = move.from().as_idx();
    our_pieces_.set(to);
    switch (piece) {
      case 0:
        pawns_.set(to);
        break;
      case 1:
        bishops_.set(to);
        break;
      case 2:
        rooks_.set(to);
        break;
      case 3:
        rooks_.set(to);
        bishops_.set(to);
        break;
    }
    pawns_ &= kPawnMask; 
    RemoveFromPocket(piece);
    return 0xFF;
  }
  assert(our_pieces_.intersects(BitBoard::FromSquare(move.from())));
#ifndef NDEBUG
  absl::Cleanup validate = [&] {
    if (!IsValid()) {
      CERR << "Move " + move.ToString(true) +
                  " resulted in invalid board: " + DebugString();
      assert(false);
    }
  };
#endif
  const Square& from = move.from();
  const Square& to = move.to();
  const Rank from_rank = from.rank();
  const File from_file = from.file();
  const Rank to_rank = to.rank();
  const File to_file = to.file();
  
  // Castlings.
  if (from == our_king_) {
    castlings_.reset_we_can_00();
    castlings_.reset_we_can_000();
    auto do_castling = [this](File king_dst, Square rook_src, File rook_dst) {
      // Remove en passant flags.
      pawns_ &= kPawnMask;
      our_pieces_.reset(our_king_);
      our_pieces_.reset(rook_src);
      rooks_.reset(rook_src);
      our_king_ = Square(king_dst, kRank1);
      our_pieces_.set(our_king_);
      Square rook_dst_sq(rook_dst, kRank1);
      our_pieces_.set(rook_dst_sq);
      rooks_.set(rook_dst_sq);
    };
    if (move.is_castling()) {
      // Castling.
      if (to_file > from_file) {
        // Kingside.
        do_castling(kFileG, to, kFileF);
      } else {
        // Queenside.
        do_castling(kFileC, to, kFileD);
      }
      return 0xFF;
    }
  }

  // Move in our pieces.
  our_pieces_.reset(from);
  our_pieces_.set(to);
  if (promoted_pieces_.get(from)) {
    promoted_pieces_.reset(from);
    promoted_pieces_.set(to);
  }

  uint8_t captured{0xFF};

  // Remove captured piece.
  bool is_captured = their_pieces_.get(to);
  if (is_captured) {
    captured = 0;
    if (bishops().get(to)) { captured = 1; }
    if (rooks().get(to)) { captured += 2; }
    if (knights().get(to)) { captured = 4; }
    their_pieces_.reset(to);
    rooks_.reset(to);
    bishops_.reset(to);
    pawns_.reset(to);
    // Promoted pieces capture as pawn in bughouse
    if (promoted_pieces_.get(to)){ 
      promoted_pieces_.reset(to);
      captured = 0; 
    }
    // TODO: remove from promoted_piece AND fix captured if promoted
    if (to == Square(castlings_.their_kingside_rook, kRank8)) {
      castlings_.reset_they_can_00();
    }
    if (to == Square(castlings_.their_queenside_rook, kRank8)) {
      castlings_.reset_they_can_000();
    }
  }

  // En passant.
  if (move.is_en_passant()) {
    const Square ep_pawn(to_file, kRank5);
    pawns_.reset(ep_pawn);
    their_pieces_.reset(ep_pawn);
    captured = 0;
  }

  // Remove en passant flags.
  pawns_ &= kPawnMask; 

  // King, non-castling move
  if (from == our_king_) {
    our_king_ = to;
    return captured;
  }

  // Promotion.
  if (move.is_promotion()) {
    switch (move.promotion().idx) {
      case kRook.idx:
        rooks_.set(to);
        break;
      case kBishop.idx:
        bishops_.set(to);
        break;
      case kQueen.idx:
        rooks_.set(to);
        bishops_.set(to);
        break;
      default:;
    promoted_pieces_.set(to);
    }
    pawns_.reset(from);
    return captured;
  }

  // Reset castling rights.
  if (from_rank == kRank1 && rooks_.get(from)) {
    if (from_file == castlings_.our_queenside_rook) {
      castlings_.reset_we_can_000();
    }
    if (from_file == castlings_.our_kingside_rook) {
      castlings_.reset_we_can_00();
    }
  }

  // Ordinary move.
  rooks_.set_if(to, rooks_.get(from));
  bishops_.set_if(to, bishops_.get(from));
  pawns_.set_if(to, pawns_.get(from));
  rooks_.reset(from);
  bishops_.reset(from);
  pawns_.reset(from);

  // Set en passant flag.
  if (to_rank - from_rank == 2 && pawns_.get(to)) {
    Square ep_sq(to_file, to_rank - 1);
    if (kPawnAttacks[ep_sq.as_idx()].intersects(their_pieces_ & pawns_)) {
      pawns_.set(Square(to_file, kRank1));
    }
  }
  return captured;
}
// TODO: Rules change with clocks
//pbrqn
bool BugBoard::HasMatingMaterial() const {
  if (ChessBoard::HasMatingMaterial()) { return true; }
  uint16_t our_pocket = static_cast<uint16_t>(pockets_);
  uint16_t their_pocket = static_cast<uint16_t>(pockets_ >> 16);
  if( (our_pocket & kPocketMasks[2]) > 0 || (our_pocket & kPocketMasks[0]) > 0 ||
      (their_pocket & kPocketMasks[2]) > 0 || (their_pocket & kPocketMasks[0]) > 0) {
    return true;
  }
  auto pocket_count = [this](uint16_t pocket) {
    uint8_t count{0};
    for (size_t i=0;i<5;i++) {
      count += (kPocketMasks[i] & pocket) >> kPocketMasks[i];
    }
    return count;
  };
  if ((our_pieces_ | their_pieces_).count() + pocket_count(our_pocket) + pocket_count(their_pocket) < 4) {
    return false;
  }
  if (!(knights().empty()) || (our_pocket & kPocketMasks[4]) > 0 || (their_pocket & kPocketMasks[4]) > 0) {
    return true;
  }
  if (pocket_count(our_pocket) + pocket_count(their_pocket) == 0) {
    constexpr BitBoard kLightSquares(0x55AA55AA55AA55AAULL);
    constexpr BitBoard kDarkSquares(0xAA55AA55AA55AA55ULL);

    const bool light_bishop = bishops_.intersects(kLightSquares);
    const bool dark_bishop = bishops_.intersects(kDarkSquares);
    return light_bishop && dark_bishop;
  }
  return true;
}
bool BugBoard::IsLegalMove(Move move,
                             const KingAttackInfo& king_attack_info) const {
  const auto& from = move.from();
  const auto& to = move.to();        

  // - Placing from pocket -
  if (move.isBugMove()) {
    if (king_attack_info.in_check()) {
      if (king_attack_info.in_double_check()) {
        // Only a king move can resolve the double check.
        return false;
      } else {
        // Only one attacking piece gives check.
        // Our piece is free to move (not pinned). Check if the attacker is
        // captured or interposed after the piece has moved to its destination
        // square.
        return king_attack_info.is_on_attack_line(to);
      }
    } 
    return true;
  }

  // - Regular moves -
  return ChessBoard::IsLegalMove(move, king_attack_info);
}

//TODO: add bughouse support
Move ChessBoard::ParseMove(std::string_view move_str) const {
  auto complain = [&move_str](std::string_view reason) {
    throw Exception("Invalid move (" + std::string(reason) +
                    "): " + std::string(move_str));
  };
  if (move_str.size() < 4 || move_str.size() > 5) complain("wrong move size");
  File from_file = File::Parse(move_str[0]);
  Rank from_rank = Rank::Parse(move_str[1]);
  File to_file = File::Parse(move_str[2]);
  Rank to_rank = Rank::Parse(move_str[3]);
  if (!from_file.IsValid() || !from_rank.IsValid() || !to_file.IsValid() ||
      !to_rank.IsValid()) {
    complain("bad square");
  }
  if (flipped_) {
    from_rank.Flip();
    to_rank.Flip();
  }
  Square from(from_file, from_rank);
  Square to(to_file, to_rank);
  if (!our_pieces_.get(from)) complain("no piece to move");

  // Pawns at back ranks are used to encode en-passant, that's why we need to
  // check that a piece doesn't go from there.
  if (pawns_.get(from) && (from_rank != kRank1 && from_rank != kRank8) &&
      (to_rank == kRank1 || to_rank == kRank8)) {
    // Promotion.
    PieceType promotion =
        move_str.size() > 4 ? PieceType::Parse(move_str[4]) : kKnight;
    if (!promotion.CanPromoteInto()) complain("invalid promotion");
    return Move::WhitePromotion(from, to, promotion);
  }
  if (from == our_king_ && our_pieces_.get(to)) {
    // FRC-style castling.
    return Move::WhiteCastling(from.file(), to.file());
  }
  if (from == our_king_ && from == kSquareE1 && to == kSquareG1) {
    // Kingside castling.
    return Move::WhiteCastling(from.file(), kFileH);
  }
  if (from == our_king_ && from == kSquareE1 && to == kSquareC1) {
    // Qeenside castling.
    return Move::WhiteCastling(from.file(), kFileA);
  }
  if (from.file() != to.file() && pawns().get(from) && !their_pieces_.get(to)) {
    // En passant.
    return Move::WhiteEnPassant(from, to);
  }
  return Move::White(from, to);
}

namespace {
//TODO: add bughouse support
char GetPieceAt(const lczero::ChessBoard& board, Square square) {
  char c = '\0';
  if (board.ours().get(square) || board.theirs().get(square)) {
    if (board.pawns().get(square)) {
      c = 'P';
    } else if (board.kings().get(square)) {
      c = 'K';
    } else if (board.bishops().get(square)) {
      c = 'B';
    } else if (board.queens().get(square)) {
      c = 'Q';
    } else if (board.rooks().get(square)) {
      c = 'R';
    } else {
      c = 'N';
    }
    if (board.theirs().get(square)) {
      c = std::tolower(c);  // Capitals are for white.
    }
  }
  return c;
}

}  // namespace
}  // namespace lczero
