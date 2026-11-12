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
#include "utils/logging.h"

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

void BugBoard::SetFromFen(std::string_view fen, int* moves) {
  ChessBoard::SetFromFen(fen, nullptr, moves);
}

Move BugBoard::ParseMove(std::string_view move_str) const {
  if (move_str.find('@') == std::string_view::npos) {
    return ChessBoard::ParseMove(move_str);
  }
  if (move_str.size() != 4 || move_str[1] != '@' ||
      move_str[2] < 'a' || move_str[2] > 'h' ||
      move_str[3] < '1' || move_str[3] > '8') {
    throw Exception("Expected a drop such as N@f3");
  }

  // Use the same piece order as the pocket and drop generator.
  constexpr std::string_view kDropPieces = "pbrqn";
  const char piece =
      static_cast<char>(std::tolower(static_cast<unsigned char>(move_str[0])));
  const auto piece_index = kDropPieces.find(piece);
  if (piece_index == std::string_view::npos) {
    throw Exception("Invalid piece in drop: " + std::string(move_str));
  }

  Square to = Square::Parse(move_str.substr(2));
  if (flipped()) to.Flip();
  return Move::Bug(Square::FromIdx(static_cast<uint8_t>(piece_index)), to);
}

bool BugBoard::SamePositionForRepetition(const BugBoard& other) const {
  const auto& board = static_cast<const ChessBoard&>(*this);
  const auto& other_board = static_cast<const ChessBoard&>(other);
  if (en_passant() == other.en_passant()) return board == other_board;

  auto normalize = [](BugBoard copy) {
    if (!copy.en_passant().empty()) {
      const auto moves = copy.GenerateLegalMoves();
      const bool can_capture = std::any_of(
          moves.begin(), moves.end(),
          [](Move move) { return move.is_en_passant(); });
      if (!can_capture) copy.pawns_ &= kPawnMask;
    }
    return copy;
  };
  return static_cast<const ChessBoard&>(normalize(*this)) ==
         static_cast<const ChessBoard&>(normalize(other));
}

void BugBoard::Clear() { *this = BugBoard(); }

void BugBoard::Mirror() {
  ChessBoard::Mirror();
  promoted_pieces_.Mirror();
  pockets_ = ((pockets_ & 0xFFFF0000) >> 16) | ((pockets_ & 0x0000FFFF) << 16);
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
      (promoted() - all).as_int()) {
    return false;
  }
  uint32_t pockets = GetPockets();
  uint16_t our_pocket = static_cast<uint16_t>(pockets);
  uint16_t their_pocket = static_cast<uint16_t>(pockets >> 16);
  if ((GetPocketCount(0, our_pocket) > 16) || 
      (GetPocketCount(0, their_pocket) > 16) ||
      (GetPocketCount(1, our_pocket) > 4) || 
      (GetPocketCount(1, their_pocket) > 4) ||
      (GetPocketCount(2, our_pocket) > 4) || 
      (GetPocketCount(2, their_pocket) > 4) ||
      (GetPocketCount(3, our_pocket) > 2) || 
      (GetPocketCount(3, their_pocket) > 2) ||
      (GetPocketCount(4, our_pocket) > 4) || 
      (GetPocketCount(4, their_pocket) > 4)) {
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
  uint8_t captured{0xFF};

  // Remove captured piece.
  bool is_captured = their_pieces_.get(to);
  if (is_captured) {
    captured = 0;
    // Captured promoted pieces return to the partner as pawns.
    if (!promoted_pieces_.get(to)) {
      if (bishops().get(to)) captured = 1;
      if (rooks().get(to)) captured = 2;
      if (queens().get(to)) captured = 3;
      if (knights().get(to)) captured = 4;
    }
    their_pieces_.reset(to);
    rooks_.reset(to);
    bishops_.reset(to);
    pawns_.reset(to);
    promoted_pieces_.reset(to);
    if (to == Square(castlings_.their_kingside_rook, kRank8)) {
      castlings_.reset_they_can_00();
    }
    if (to == Square(castlings_.their_queenside_rook, kRank8)) {
      castlings_.reset_they_can_000();
    }
  }

  if (promoted_pieces_.get(from)) {
    promoted_pieces_.reset(from);
    promoted_pieces_.set(to);
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
      default:
        break;
    }
    promoted_pieces_.set(to);
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

}  // namespace lczero
