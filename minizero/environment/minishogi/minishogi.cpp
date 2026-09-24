#include "minishogi.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace minizero::env::minishogi {

namespace {

    inline int toPosition(int row, int col) { return row * kMinishogiBoardSize + col; }
    inline int rowOf(int position) { return position / kMinishogiBoardSize; }
    inline int colOf(int position) { return position % kMinishogiBoardSize; }
    inline bool onBoard(int row, int col) { return row >= 0 && row < kMinishogiBoardSize && col >= 0 && col < kMinishogiBoardSize; }

    // Player1 sits at the bottom (row 4) and advances towards row 0; the
    // promotion zone is the opponent's back rank only
    inline int enemyBackRowOf(Player player) { return (player == Player::kPlayer1 ? 0 : kMinishogiBoardSize - 1); }

    const char* kPieceLetter = "PSGBR";

    inline bool isPromotable(PieceType type) { return type == PieceType::kPawn || type == PieceType::kSilver || type == PieceType::kBishop || type == PieceType::kRook; }
    inline PieceType promote(PieceType type) { return static_cast<PieceType>(static_cast<int>(type) + kPromotionOffset); }
    // what a captured piece becomes in hand
    inline PieceType unpromote(PieceType type)
    {
        int t = static_cast<int>(type);
        return (t >= kPromotionOffset && t < static_cast<int>(PieceType::kKing)) ? static_cast<PieceType>(t - kPromotionOffset) : type;
    }

    // can this piece move in direction `rel` (0 = forward, clockwise, as seen by
    // its owner) by `distance` squares, ignoring what is in the way
    bool canMove(PieceType type, int rel, int distance)
    {
        const bool orthogonal = (rel % 2 == 0);
        const bool diagonal = !orthogonal;
        switch (type) {
            case PieceType::kKing: return distance == 1;
            case PieceType::kGold:
            case PieceType::kPPawn:
            case PieceType::kPSilver:
                return distance == 1 && rel != 3 && rel != 5; // all but the two backward diagonals
            case PieceType::kSilver: return distance == 1 && (diagonal || rel == 0);
            case PieceType::kPawn: return distance == 1 && rel == 0;
            case PieceType::kBishop: return diagonal;
            case PieceType::kRook: return orthogonal;
            case PieceType::kPBishop: return diagonal || distance == 1;
            case PieceType::kPRook: return orthogonal || distance == 1;
            default: return false;
        }
    }

    // board directions are absolute; turn them into the owner's point of view
    inline int relativeDirection(int direction, Player owner) { return owner == Player::kPlayer1 ? direction : (direction + 4) % 8; }

    // ---- the network's coordinates, copied from 9x9 shogi ----
    // shogi.cpp numbers a square rank * 9 + (file - 1): row 0 is rank a, column
    // 0 is file 1. Probed on the built 9x9 binary: the sente rook on 2b lights
    // (1, 1). Our board keeps file 5 at column 0, so the column is mirrored.
    inline int toNetSquare(int position) { return rowOf(position) * kMinishogiBoardSize + (kMinishogiBoardSize - 1 - colOf(position)); }
    inline int fromNetSquare(int net) { return toNetSquare(net); } // the mirror is its own inverse
    // for White everything is turned 180 degrees, as `80 - sq` in shogi.h
    inline int relativeSquare(int net, Player player) { return player == Player::kPlayer2 ? kMinishogiBoardArea - 1 - net : net; }

    // move directions in network coordinates, in shogi.h's
    // map_dx_dy_to_direction_id order without its two knight slots:
    // up, down, left, right, up-left, up-right, down-left, down-right
    const int kNetDx[8] = {0, 0, -1, 1, -1, 1, -1, 1};
    const int kNetDy[8] = {-1, 1, 0, 0, -1, -1, 1, 1};

    int netDirectionId(int dx, int dy)
    {
        int distance = std::max(std::abs(dx), std::abs(dy));
        if (distance == 0 || distance > kMinishogiMaxDistance) { return -1; }
        if (dx != 0 && dy != 0 && std::abs(dx) != std::abs(dy)) { return -1; } // not a line
        for (int group = 0; group < 8; ++group) {
            if (kNetDx[group] * distance == dx && kNetDy[group] * distance == dy) { return group * kMinishogiMaxDistance + distance - 1; }
        }
        return -1;
    }

    // the network's piece and hand orders, shogi.cpp's with lance and knight removed:
    // pieces P L N S G B R K +P +L +N +S +B +R  ->  P S G B R K +P +S +B +R
    // hands  P L N S B R G                      ->  P S B R G
    const int kPiecePlane[static_cast<int>(PieceType::kPieceTypeSize)] = {0, 1, 2, 3, 4, 6, 7, -1, 8, 9, 5};
    const PieceType kNetHandOrder[kMinishogiNumDroppable] = {PieceType::kPawn, PieceType::kSilver, PieceType::kBishop, PieceType::kRook, PieceType::kGold};

    std::string squareString(int position)
    {
        std::string s;
        s += static_cast<char>('0' + (kMinishogiBoardSize - colOf(position))); // files 5..1 from the left
        s += static_cast<char>('a' + rowOf(position));                         // ranks a..e from the top
        return s;
    }

} // namespace

Move decodeAction(int action_id, Player player)
{
    Move move;
    if (action_id < 0 || action_id >= kMinishogiPolicySize) { return move; }
    if (action_id < kMinishogiDropActionSize) {
        move.drop_ = true;
        move.drop_type_ = kNetHandOrder[action_id / kMinishogiBoardArea];
        move.to_ = fromNetSquare(relativeSquare(action_id % kMinishogiBoardArea, player));
        move.valid_ = true;
        return move;
    }
    int id = action_id - kMinishogiDropActionSize;
    int rel_from = id / kMinishogiMovePerSquare;
    int direction_id = (id % kMinishogiMovePerSquare) / 2;
    int group = direction_id / kMinishogiMaxDistance, distance = direction_id % kMinishogiMaxDistance + 1;
    int to_row = rel_from / kMinishogiBoardSize + kNetDy[group] * distance;
    int to_col = rel_from % kMinishogiBoardSize + kNetDx[group] * distance;
    if (!onBoard(to_row, to_col)) { return move; }
    move.from_ = fromNetSquare(relativeSquare(rel_from, player));
    move.to_ = fromNetSquare(relativeSquare(to_row * kMinishogiBoardSize + to_col, player));
    move.promote_ = (id % 2) == 1;
    move.valid_ = true;
    return move;
}

int encodeMove(const Move& move, Player player)
{
    if (!move.valid_) { return -1; }
    if (move.drop_) {
        int slot = static_cast<int>(std::find(kNetHandOrder, kNetHandOrder + kMinishogiNumDroppable, move.drop_type_) - kNetHandOrder);
        if (slot == kMinishogiNumDroppable) { return -1; }
        return slot * kMinishogiBoardArea + relativeSquare(toNetSquare(move.to_), player);
    }
    int rel_from = relativeSquare(toNetSquare(move.from_), player), rel_to = relativeSquare(toNetSquare(move.to_), player);
    int direction_id = netDirectionId(rel_to % kMinishogiBoardSize - rel_from % kMinishogiBoardSize, rel_to / kMinishogiBoardSize - rel_from / kMinishogiBoardSize);
    if (direction_id < 0) { return -1; }
    return kMinishogiDropActionSize + rel_from * kMinishogiMovePerSquare + direction_id * 2 + (move.promote_ ? 1 : 0);
}

std::string getMinishogiActionString(int action_id, Player player)
{
    Move move = decodeAction(action_id, player);
    if (!move.valid_) { return "??"; }
    if (move.drop_) { return std::string(1, kPieceLetter[static_cast<int>(move.drop_type_)]) + "*" + squareString(move.to_); }
    return squareString(move.from_) + squareString(move.to_) + (move.promote_ ? "+" : "");
}

int getMinishogiActionID(const std::string& action_string, Player player)
{
    for (int action_id = 0; action_id < kMinishogiPolicySize; ++action_id) {
        if (getMinishogiActionString(action_id, player) == action_string) { return action_id; }
    }
    return -1;
}

void MinishogiEnv::reset()
{
    hands_.reset();
    board_.fill(Piece());
    // point symmetric. Player1's back rank from file 5 to file 1 is
    // king, gold, silver, bishop, rook, with the pawn in front of the king.
    const PieceType back[kMinishogiBoardSize] = {PieceType::kKing, PieceType::kGold, PieceType::kSilver, PieceType::kBishop, PieceType::kRook};
    for (int col = 0; col < kMinishogiBoardSize; ++col) {
        board_[toPosition(kMinishogiBoardSize - 1, col)] = {back[col], Player::kPlayer1};
        board_[toPosition(0, kMinishogiBoardSize - 1 - col)] = {back[col], Player::kPlayer2};
    }
    board_[toPosition(kMinishogiBoardSize - 2, 0)] = {PieceType::kPawn, Player::kPlayer1};
    board_[toPosition(1, kMinishogiBoardSize - 1)] = {PieceType::kPawn, Player::kPlayer2};
    startFrom(Player::kPlayer1);
}

// start the game from board_ and hands_ as they are now
void MinishogiEnv::startFrom(Player turn)
{
    turn_ = turn;
    winner_ = Player::kPlayerNone;
    is_draw_ = false;
    actions_.clear();
    position_history_.assign(1, toPositionString());
    state_history_.assign(1, {board_, hands_});
    check_history_.assign(1, false);
}

bool MinishogiEnv::setFromSFEN(const std::string& sfen)
{
    // e.g. "rbsgk/4p/5/P4/KGSBR b - 1": ranks a..e, files 5..1 left to right,
    // upper case for sente, '+' before a promoted piece, then side and hands
    std::istringstream iss(sfen);
    std::string placement, side, hands;
    if (!(iss >> placement >> side >> hands) || (side != "b" && side != "w")) { return false; }

    auto typeOf = [](char c, PieceType& type) {
        switch (std::toupper(static_cast<unsigned char>(c))) {
            case 'P': type = PieceType::kPawn; return true;
            case 'S': type = PieceType::kSilver; return true;
            case 'G': type = PieceType::kGold; return true;
            case 'B': type = PieceType::kBishop; return true;
            case 'R': type = PieceType::kRook; return true;
            case 'K': type = PieceType::kKing; return true;
            default: return false;
        }
    };

    Board board;
    board.fill(Piece());
    int row = 0, col = 0;
    bool promoted = false;
    for (char c : placement) {
        if (c == '/') {
            if (col != kMinishogiBoardSize) { return false; }
            ++row, col = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            col += c - '0';
        } else if (c == '+') {
            promoted = true;
        } else {
            PieceType type;
            if (!typeOf(c, type) || !onBoard(row, col)) { return false; }
            if (promoted) {
                if (!isPromotable(type)) { return false; }
                type = promote(type);
            }
            board[toPosition(row, col++)] = {type, std::isupper(static_cast<unsigned char>(c)) ? Player::kPlayer1 : Player::kPlayer2};
            promoted = false;
        }
    }
    if (row != kMinishogiBoardSize - 1 || col != kMinishogiBoardSize) { return false; }

    GamePair<Hand> hand_pair;
    hand_pair.reset();
    if (hands != "-") {
        int count = 0;
        for (char c : hands) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                count = count * 10 + (c - '0');
                continue;
            }
            PieceType type;
            if (!typeOf(c, type) || type == PieceType::kKing) { return false; }
            hand_pair.get(std::isupper(static_cast<unsigned char>(c)) ? Player::kPlayer1 : Player::kPlayer2)[static_cast<int>(type)] += (count == 0 ? 1 : count);
            count = 0;
        }
    }

    board_ = board;
    hands_ = hand_pair;
    startFrom(side == "b" ? Player::kPlayer1 : Player::kPlayer2);
    if (!hasLegalAction()) { winner_ = getNextPlayer(turn_, kMinishogiNumPlayer); }
    return true;
}

bool MinishogiEnv::isAttacked(const Board& board, int position, Player by) const
{
    for (int from = 0; from < kMinishogiBoardArea; ++from) {
        const Piece& piece = board[from];
        if (piece.isEmpty() || piece.owner_ != by) { continue; }
        for (int direction = 0; direction < 8; ++direction) {
            int rel = relativeDirection(direction, by);
            for (int distance = 1; distance <= kMinishogiMaxDistance; ++distance) {
                int row = rowOf(from) + kDirectionRow[direction] * distance, col = colOf(from) + kDirectionCol[direction] * distance;
                if (!onBoard(row, col) || !canMove(piece.type_, rel, distance)) { break; }
                int to = toPosition(row, col);
                if (to == position) { return true; }
                if (!board[to].isEmpty()) { break; } // blocked beyond here
            }
        }
    }
    return false;
}

bool MinishogiEnv::isKingAttacked(const Board& board, Player player) const
{
    for (int position = 0; position < kMinishogiBoardArea; ++position) {
        if (board[position].owner_ == player && board[position].type_ == PieceType::kKing) {
            return isAttacked(board, position, getNextPlayer(player, kMinishogiNumPlayer));
        }
    }
    return false;
}

bool MinishogiEnv::isPseudoLegal(const Move& move, Player player) const
{
    if (!move.valid_) { return false; }

    if (move.drop_) {
        PieceType type = move.drop_type_;
        int to = move.to_;
        if (hands_.get(player)[static_cast<int>(type)] == 0 || !board_[to].isEmpty()) { return false; }
        if (type == PieceType::kPawn) {
            // 行き所のない駒: a pawn on the last rank could never move
            if (rowOf(to) == enemyBackRowOf(player)) { return false; }
            // 二歩: an unpromoted pawn of ours already stands on this file
            for (int row = 0; row < kMinishogiBoardSize; ++row) {
                const Piece& p = board_[toPosition(row, colOf(to))];
                if (p.owner_ == player && p.type_ == PieceType::kPawn) { return false; }
            }
        }
        return true;
    }

    int from = move.from_, to = move.to_;
    const Piece& piece = board_[from];
    if (piece.isEmpty() || piece.owner_ != player) { return false; }
    int dr = rowOf(to) - rowOf(from), dc = colOf(to) - colOf(from);
    int distance = std::max(std::abs(dr), std::abs(dc));
    int direction = -1;
    for (int d = 0; d < 8; ++d) {
        if (kDirectionRow[d] * distance == dr && kDirectionCol[d] * distance == dc) { direction = d; }
    }
    if (direction < 0 || !canMove(piece.type_, relativeDirection(direction, player), distance)) { return false; }
    for (int step = 1; step <= distance; ++step) {
        const Piece& there = board_[toPosition(rowOf(from) + kDirectionRow[direction] * step, colOf(from) + kDirectionCol[direction] * step)];
        if (step < distance && !there.isEmpty()) { return false; }        // path must be clear
        if (step == distance && there.owner_ == player) { return false; } // cannot take our own
    }

    bool in_zone = (rowOf(from) == enemyBackRowOf(player) || rowOf(to) == enemyBackRowOf(player));
    if (move.promote_) { return isPromotable(piece.type_) && in_zone; }
    // 行き所のない駒: a pawn reaching the last rank must promote
    if (piece.type_ == PieceType::kPawn && rowOf(to) == enemyBackRowOf(player)) { return false; }
    return true;
}

void MinishogiEnv::applyMove(const Move& move, Player mover, Board& board, GamePair<Hand>& hands) const
{
    if (move.drop_) {
        --hands.get(mover)[static_cast<int>(move.drop_type_)];
        board[move.to_] = {move.drop_type_, mover};
        return;
    }
    Piece moving = board[move.from_];
    const Piece& captured = board[move.to_];
    // a captured king never reaches a hand: legal play cannot capture one
    if (!captured.isEmpty() && captured.type_ != PieceType::kKing) { ++hands.get(mover)[static_cast<int>(unpromote(captured.type_))]; }
    if (move.promote_) { moving.type_ = promote(moving.type_); }
    board[move.from_] = Piece();
    board[move.to_] = moving;
}

bool MinishogiEnv::isUchifuzume(const Move& move) const
{
    // 打ち歩詰め only concerns a pawn drop that gives check
    if (!move.drop_ || move.drop_type_ != PieceType::kPawn) { return false; }
    int to = move.to_;
    int front_row = rowOf(to) + (turn_ == Player::kPlayer1 ? -1 : 1);
    if (!onBoard(front_row, colOf(to))) { return false; }
    const Piece& target = board_[toPosition(front_row, colOf(to))];
    Player opponent = getNextPlayer(turn_, kMinishogiNumPlayer);
    if (target.owner_ != opponent || target.type_ != PieceType::kKing) { return false; }

    // it is mate if the opponent has no answer. A pawn checks from the adjacent
    // square, so no drop can interpose; only board moves need trying, which
    // also keeps this from recursing into another uchifuzume test.
    MinishogiEnv after = *this;
    applyMove(move, turn_, after.board_, after.hands_);
    after.turn_ = opponent;
    for (int action_id = kMinishogiDropActionSize; action_id < kMinishogiPolicySize; ++action_id) {
        Move reply = decodeAction(action_id, opponent);
        if (!after.isPseudoLegal(reply, opponent)) { continue; }
        Board board = after.board_;
        GamePair<Hand> hands = after.hands_;
        after.applyMove(reply, opponent, board, hands);
        if (!isKingAttacked(board, opponent)) { return false; }
    }
    return true;
}

bool MinishogiEnv::isLegalAction(const MinishogiAction& action) const
{
    if (winner_ != Player::kPlayerNone || is_draw_) { return false; }
    if (action.getPlayer() != turn_) { return false; }
    Move move = action.decode();
    if (!isPseudoLegal(move, turn_)) { return false; }
    Board board = board_;
    GamePair<Hand> hands = hands_;
    applyMove(move, turn_, board, hands);
    if (isKingAttacked(board, turn_)) { return false; } // may not leave our king in check
    return !isUchifuzume(move);
}

std::vector<MinishogiAction> MinishogiEnv::getLegalActions() const
{
    std::vector<MinishogiAction> actions;
    if (isTerminal()) { return actions; }
    // drops only for piece types in hand
    for (int slot = 0; slot < kMinishogiNumDroppable; ++slot) {
        if (hands_.get(turn_)[static_cast<int>(kNetHandOrder[slot])] == 0) { continue; }
        for (int net_to = 0; net_to < kMinishogiBoardArea; ++net_to) {
            MinishogiAction action(slot * kMinishogiBoardArea + net_to, turn_);
            if (isLegalAction(action)) { actions.emplace_back(action); }
        }
    }
    // board moves only from squares holding our pieces
    for (int from = 0; from < kMinishogiBoardArea; ++from) {
        if (board_[from].owner_ != turn_) { continue; }
        int base = kMinishogiDropActionSize + relativeSquare(toNetSquare(from), turn_) * kMinishogiMovePerSquare;
        for (int offset = 0; offset < kMinishogiMovePerSquare; ++offset) {
            MinishogiAction action(base + offset, turn_);
            if (isLegalAction(action)) { actions.emplace_back(action); }
        }
    }
    return actions;
}

bool MinishogiEnv::hasLegalAction() const
{
    return !getLegalActions().empty();
}

bool MinishogiEnv::act(const MinishogiAction& action)
{
    if (!isLegalAction(action)) { return false; }
    applyMove(action.decode(), turn_, board_, hands_);
    actions_.push_back(action);
    turn_ = action.nextPlayer();

    position_history_.push_back(toPositionString());
    state_history_.push_back({board_, hands_});
    check_history_.push_back(isKingAttacked(board_, turn_));
    settleGameEnd();
    return true;
}

bool MinishogiEnv::act(const std::vector<std::string>& action_string_args)
{
    return act(MinishogiAction(action_string_args));
}

int MinishogiEnv::repetitionCountAt(int index) const
{
    // occurrences of position `index` up to and including it, as shogi.cpp's
    // repetition_history_ (1 for a first occurrence)
    return static_cast<int>(std::count(position_history_.begin(), position_history_.begin() + index + 1, position_history_[index]));
}

void MinishogiEnv::settleGameEnd()
{
    // 千日手: the fourth occurrence of a position, same side to move, ends the
    // game. Sente loses, unless one side checked on every move of the cycle, in
    // which case the checking side loses.
    int last = static_cast<int>(position_history_.size()) - 1;
    if (repetitionCountAt(last) >= 4) {
        int first = static_cast<int>(std::find(position_history_.begin(), position_history_.end(), position_history_[last]) - position_history_.begin());
        // position i follows actions_[i - 1]; the game may have started with either side to move
        bool sente_always_checked = true, gote_always_checked = true;
        for (int ply = first + 1; ply <= last; ++ply) {
            bool by_sente = (actions_[ply - 1].getPlayer() == Player::kPlayer1);
            if (!check_history_[ply]) { (by_sente ? sente_always_checked : gote_always_checked) = false; }
        }
        if (sente_always_checked) {
            winner_ = Player::kPlayer2;
        } else if (gote_always_checked) {
            winner_ = Player::kPlayer1;
        } else {
            winner_ = Player::kPlayer2;
        }
        return;
    }

    // mate, or no legal move at all: the side to move loses
    if (!hasLegalAction()) {
        winner_ = getNextPlayer(turn_, kMinishogiNumPlayer);
        return;
    }

    // not a rule of the game: a guard so self-play cannot run unbounded
    if (config::env_minishogi_max_moves > 0 && static_cast<int>(actions_.size()) >= config::env_minishogi_max_moves) { is_draw_ = true; }
}

bool MinishogiEnv::isTerminal() const
{
    return winner_ != Player::kPlayerNone || is_draw_;
}

float MinishogiEnv::getEvalScore(bool is_resign) const
{
    Player result = is_resign ? getNextPlayer(turn_, kMinishogiNumPlayer) : winner_;
    switch (result) {
        case Player::kPlayer1: return 1.0f;
        case Player::kPlayer2: return -1.0f;
        default: return 0.0f;
    }
}

std::vector<float> MinishogiEnv::getFeatures(utils::Rotation rotation) const
{
    /*
       266 channels, shogi.cpp's getFeatures() at 5x5. For each of 8 history
       steps t (t = 0 is now; steps before the start stay zero), at t * 33:
          0~ 9. pieces of the side to move: P S G B R K +P +S +B +R
         10~19. opponent pieces, same order
         20~22. repetition: plane 20 + (occurrences so far, capped at 3) - 1
         23~27. hand of the side to move, raw counts: P S B R G
         28~32. opponent hand, same order
       then
           264. 1 if sente is to move, else 0
           265. plies played / 512
       Every step is seen from the side to move NOW: for White the squares are
       turned 180 degrees, as in shogi.cpp.
    */
    const int area = kMinishogiBoardArea;
    Player opponent = getNextPlayer(turn_, kMinishogiNumPlayer);
    std::vector<float> features(kMinishogiNumInputChannels * area, 0.0f);

    for (int t = 0; t < kMinishogiHistory; ++t) {
        int index = static_cast<int>(state_history_.size()) - 1 - t;
        if (index < 0) { continue; }
        const int base = t * kMinishogiChannelsPerStep;
        const Board& board = state_history_[index].first;
        const GamePair<Hand>& hands = state_history_[index].second;

        for (int position = 0; position < area; ++position) {
            const Piece& piece = board[position];
            if (piece.isEmpty()) { continue; }
            int channel = base + kPiecePlane[static_cast<int>(piece.type_)] + (piece.owner_ == turn_ ? 0 : kMinishogiNumPiecePlane);
            features[channel * area + relativeSquare(toNetSquare(position), turn_)] = 1.0f;
        }

        int repetition = std::min(repetitionCountAt(index), 3);
        std::fill_n(features.begin() + (base + 2 * kMinishogiNumPiecePlane + repetition - 1) * area, area, 1.0f);

        const int hand_base = base + 2 * kMinishogiNumPiecePlane + 3;
        for (int slot = 0; slot < kMinishogiNumDroppable; ++slot) {
            int type = static_cast<int>(kNetHandOrder[slot]);
            std::fill_n(features.begin() + (hand_base + slot) * area, area, static_cast<float>(hands.get(turn_)[type]));
            std::fill_n(features.begin() + (hand_base + kMinishogiNumDroppable + slot) * area, area, static_cast<float>(hands.get(opponent)[type]));
        }
    }

    const int global = kMinishogiHistory * kMinishogiChannelsPerStep;
    std::fill_n(features.begin() + global * area, area, turn_ == Player::kPlayer1 ? 1.0f : 0.0f);
    std::fill_n(features.begin() + (global + 1) * area, area, static_cast<float>(actions_.size()) / kMinishogiMoveCountScale);
    return features;
}

// MuZero only; empty, as in shogi.cpp
std::vector<float> MinishogiEnv::getActionFeatures(const MinishogiAction& action, utils::Rotation rotation) const
{
    return {};
}

std::string MinishogiEnv::toPositionString() const
{
    std::ostringstream oss;
    for (int position = 0; position < kMinishogiBoardArea; ++position) {
        const Piece& piece = board_[position];
        if (piece.isEmpty()) {
            oss << '.';
        } else {
            char letter = static_cast<char>('A' + static_cast<int>(piece.type_)); // one letter per type
            oss << (piece.owner_ == Player::kPlayer1 ? letter : static_cast<char>(std::tolower(letter)));
        }
    }
    for (int i = 0; i < kNumHandSlot; ++i) { oss << hands_.get(Player::kPlayer1)[i]; }
    for (int i = 0; i < kNumHandSlot; ++i) { oss << hands_.get(Player::kPlayer2)[i]; }
    oss << playerToChar(turn_);
    return oss.str();
}

std::string MinishogiEnv::toString() const
{
    // K king, P S G B R, promoted pieces with a leading '+'; gote in lower case
    auto pieceString = [](const Piece& piece) {
        if (piece.isEmpty()) { return std::string(" ."); }
        int t = static_cast<int>(piece.type_);
        std::string s = (piece.type_ == PieceType::kKing) ? " K" : (t >= kPromotionOffset ? std::string("+") + kPieceLetter[t - kPromotionOffset] : std::string(" ") + kPieceLetter[t]);
        if (piece.owner_ == Player::kPlayer2) { std::transform(s.begin(), s.end(), s.begin(), ::tolower); }
        return s;
    };
    auto handString = [&](Player p) {
        std::string s;
        for (int i = 0; i < kNumHandSlot; ++i) {
            for (int n = 0; n < hands_.get(p)[i]; ++n) { s += kPieceLetter[i]; }
        }
        return s.empty() ? "-" : s;
    };
    std::ostringstream oss;
    oss << "W hand: " << handString(Player::kPlayer2) << "\n";
    oss << "    5  4  3  2  1\n";
    for (int row = 0; row < kMinishogiBoardSize; ++row) {
        oss << " " << static_cast<char>('a' + row) << " ";
        for (int col = 0; col < kMinishogiBoardSize; ++col) { oss << pieceString(board_[toPosition(row, col)]) << " "; }
        oss << "\n";
    }
    oss << "B hand: " << handString(Player::kPlayer1) << "\n";
    oss << "turn: " << playerToChar(turn_) << "\n";
    return oss.str();
}

std::vector<float> MinishogiEnvLoader::getActionFeatures(const int pos, utils::Rotation rotation) const
{
    return {};
}

} // namespace minizero::env::minishogi
