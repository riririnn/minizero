#include "dobutsu.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

namespace minizero::env::dobutsu {

namespace {

    inline int toPosition(int row, int col) { return row * kDobutsuBoardWidth + col; }
    inline int rowOf(int position) { return position / kDobutsuBoardWidth; }
    inline int colOf(int position) { return position % kDobutsuBoardWidth; }
    inline bool onBoard(int row, int col) { return row >= 0 && row < kDobutsuBoardHeight && col >= 0 && col < kDobutsuBoardWidth; }

    // Player1 sits at the bottom (row 3) and advances towards row 0, so the rank it
    // promotes on -- and wins by reaching -- is the opponent's back rank
    inline int enemyBackRowOf(Player player) { return (player == Player::kPlayer1 ? 0 : kDobutsuBoardHeight - 1); }

    const char* kPieceLetter = "GECHL"; // giraffe, elephant, chick, hen, lion

    // which of the 8 directions each piece may use, as seen by Player1
    bool canStep(PieceType type, int dr, int dc)
    {
        const bool orthogonal = (dr == 0) != (dc == 0);
        const bool diagonal = (dr != 0) && (dc != 0);
        switch (type) {
            case PieceType::kLion: return true;
            case PieceType::kGiraffe: return orthogonal;
            case PieceType::kElephant: return diagonal;
            case PieceType::kChick: return dr == -1 && dc == 0;
            case PieceType::kHen: return !(dr == 1 && dc != 0); // everything but the two backward diagonals
            default: return false;
        }
    }

    // for White the board is turned 180 degrees, as `80 - sq` in shogi.h
    inline int relativeSquare(int position, Player player) { return player == Player::kPlayer2 ? kDobutsuBoardArea - 1 - position : position; }
    inline int relativeDirection(int direction, Player player) { return player == Player::kPlayer2 ? (direction + 4) % kDobutsuNumDirection : direction; }

    std::string squareString(int position)
    {
        std::string s;
        s += static_cast<char>('a' + colOf(position));
        s += static_cast<char>('1' + (kDobutsuBoardHeight - 1 - rowOf(position)));
        return s;
    }

} // namespace

Move decodeAction(int action_id, Player player)
{
    Move move;
    if (action_id < 0 || action_id >= kDobutsuPolicySize) { return move; }
    if (action_id < kDobutsuDropActionSize) {
        move.drop_ = true;
        move.drop_type_ = static_cast<PieceType>(action_id / kDobutsuBoardArea);
        move.to_ = relativeSquare(action_id % kDobutsuBoardArea, player);
        move.valid_ = true;
        return move;
    }
    int id = action_id - kDobutsuDropActionSize;
    int from = relativeSquare(id / kDobutsuNumDirection, player);
    int direction = relativeDirection(id % kDobutsuNumDirection, player);
    int to_row = rowOf(from) + kDirectionRow[direction], to_col = colOf(from) + kDirectionCol[direction];
    if (!onBoard(to_row, to_col)) { return move; }
    move.from_ = from;
    move.to_ = toPosition(to_row, to_col);
    move.valid_ = true;
    return move;
}

int encodeMove(const Move& move, Player player)
{
    if (!move.valid_) { return -1; }
    if (move.drop_) { return static_cast<int>(move.drop_type_) * kDobutsuBoardArea + relativeSquare(move.to_, player); }
    int dr = rowOf(move.to_) - rowOf(move.from_), dc = colOf(move.to_) - colOf(move.from_);
    for (int direction = 0; direction < kDobutsuNumDirection; ++direction) {
        if (kDirectionRow[direction] != dr || kDirectionCol[direction] != dc) { continue; }
        return kDobutsuDropActionSize + relativeSquare(move.from_, player) * kDobutsuNumDirection + relativeDirection(direction, player);
    }
    return -1;
}

std::string getDobutsuActionString(int action_id, Player player)
{
    Move move = decodeAction(action_id, player);
    if (!move.valid_) { return "??"; }
    if (move.drop_) { return std::string(1, kPieceLetter[static_cast<int>(move.drop_type_)]) + "*" + squareString(move.to_); }
    return squareString(move.from_) + squareString(move.to_);
}

int getDobutsuActionID(const std::string& action_string, Player player)
{
    for (int action_id = 0; action_id < kDobutsuPolicySize; ++action_id) {
        if (getDobutsuActionString(action_id, player) == action_string) { return action_id; }
    }
    return -1;
}

void DobutsuEnv::reset()
{
    hands_.reset();
    board_.fill(Piece());

    // point symmetric: Player1's back rank is elephant, lion, giraffe
    const PieceType back[kDobutsuBoardWidth] = {PieceType::kElephant, PieceType::kLion, PieceType::kGiraffe};
    for (int col = 0; col < kDobutsuBoardWidth; ++col) {
        board_[toPosition(kDobutsuBoardHeight - 1, col)] = {back[col], Player::kPlayer1};
        board_[toPosition(0, kDobutsuBoardWidth - 1 - col)] = {back[col], Player::kPlayer2};
    }
    board_[toPosition(kDobutsuBoardHeight - 2, 1)] = {PieceType::kChick, Player::kPlayer1};
    board_[toPosition(1, 1)] = {PieceType::kChick, Player::kPlayer2};
    startFrom(Player::kPlayer1);
}

void DobutsuEnv::startFrom(Player turn)
{
    turn_ = turn;
    winner_ = Player::kPlayerNone;
    is_repetition_draw_ = false;
    actions_.clear();
    position_history_.assign(1, toPositionString());
    state_history_.assign(1, {board_, hands_});
}

// e.g. "gle/1c1/1C1/ELG b - 1": rows 1..4 from Player2's back rank, upper case
// for Player1, hands as letters with an optional count
bool DobutsuEnv::setFromSFEN(const std::string& sfen)
{
    std::istringstream iss(sfen);
    std::string placement, side, hands;
    if (!(iss >> placement >> side >> hands) || (side != "b" && side != "w")) { return false; }

    auto typeOf = [](char c, PieceType& type) {
        const char* found = std::strchr(kPieceLetter, std::toupper(static_cast<unsigned char>(c)));
        if (!found) { return false; }
        type = static_cast<PieceType>(found - kPieceLetter);
        return true;
    };

    Board board;
    board.fill(Piece());
    int row = 0, col = 0;
    for (char c : placement) {
        if (c == '/') {
            if (col != kDobutsuBoardWidth) { return false; }
            ++row, col = 0;
        } else if (std::isdigit(static_cast<unsigned char>(c))) {
            col += c - '0';
        } else {
            PieceType type;
            if (!typeOf(c, type) || !onBoard(row, col)) { return false; }
            board[toPosition(row, col++)] = {type, std::isupper(static_cast<unsigned char>(c)) ? Player::kPlayer1 : Player::kPlayer2};
        }
    }
    if (row != kDobutsuBoardHeight - 1 || col != kDobutsuBoardWidth) { return false; }

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
            if (!typeOf(c, type) || static_cast<int>(type) >= kDobutsuNumDroppable) { return false; }
            hand_pair.get(std::isupper(static_cast<unsigned char>(c)) ? Player::kPlayer1 : Player::kPlayer2)[static_cast<int>(type)] += (count == 0 ? 1 : count);
            count = 0;
        }
    }

    board_ = board;
    hands_ = hand_pair;
    startFrom(side == "b" ? Player::kPlayer1 : Player::kPlayer2);
    return true;
}

bool DobutsuEnv::canReach(const Piece& piece, int from, int to) const
{
    int dr = rowOf(to) - rowOf(from), dc = colOf(to) - colOf(from);
    if (std::abs(dr) > 1 || std::abs(dc) > 1 || (dr == 0 && dc == 0)) { return false; }
    // canStep is written from Player1's side, so flip the row for Player2
    return canStep(piece.type_, piece.owner_ == Player::kPlayer1 ? dr : -dr, dc);
}

bool DobutsuEnv::isAttacked(int position, Player by) const
{
    for (int from = 0; from < kDobutsuBoardArea; ++from) {
        const Piece& piece = board_[from];
        if (piece.isEmpty() || piece.owner_ != by) { continue; }
        if (canReach(piece, from, position)) { return true; }
    }
    return false;
}

bool DobutsuEnv::isLegalAction(const DobutsuAction& action) const
{
    if (action.getPlayer() != turn_) { return false; }
    Move move = action.decode();
    if (!move.valid_) { return false; }

    if (move.drop_) {
        if (hands_.get(turn_)[static_cast<int>(move.drop_type_)] == 0) { return false; }
        return board_[move.to_].isEmpty();
    }

    const Piece& piece = board_[move.from_];
    if (piece.isEmpty() || piece.owner_ != turn_) { return false; }
    if (!board_[move.to_].isEmpty() && board_[move.to_].owner_ == turn_) { return false; }
    return canReach(piece, move.from_, move.to_);
}

std::vector<DobutsuAction> DobutsuEnv::getLegalActions() const
{
    std::vector<DobutsuAction> actions;
    if (isTerminal()) { return actions; }
    for (int action_id = 0; action_id < kDobutsuPolicySize; ++action_id) {
        DobutsuAction action(action_id, turn_);
        if (isLegalAction(action)) { actions.emplace_back(action); }
    }
    return actions;
}

bool DobutsuEnv::act(const DobutsuAction& action)
{
    if (!isLegalAction(action)) { return false; }
    Move move = action.decode();

    if (move.drop_) {
        --hands_.get(turn_)[static_cast<int>(move.drop_type_)];
        board_[move.to_] = {move.drop_type_, turn_};
    } else {
        Piece moving = board_[move.from_];
        const Piece& captured = board_[move.to_];
        if (!captured.isEmpty()) {
            if (captured.type_ == PieceType::kLion) { winner_ = turn_; }
            // a captured Hen goes back to hand as a Chick; a Lion never does
            if (captured.type_ != PieceType::kLion) {
                PieceType held = (captured.type_ == PieceType::kHen ? PieceType::kChick : captured.type_);
                ++hands_.get(turn_)[static_cast<int>(held)];
            }
        }
        // a Chick reaching the far rank always becomes a Hen
        if (moving.type_ == PieceType::kChick && rowOf(move.to_) == enemyBackRowOf(turn_)) { moving.type_ = PieceType::kHen; }
        board_[move.from_] = Piece();
        board_[move.to_] = moving;

        // walking the Lion in wins, unless the opponent can take it there
        if (winner_ == Player::kPlayerNone && moving.type_ == PieceType::kLion &&
            rowOf(move.to_) == enemyBackRowOf(turn_) && !isAttacked(move.to_, getNextPlayer(turn_, kDobutsuNumPlayer))) {
            winner_ = turn_;
        }
    }

    actions_.push_back(action);
    turn_ = action.nextPlayer();

    std::string position = toPositionString();
    if (std::count(position_history_.begin(), position_history_.end(), position) >= 2) { is_repetition_draw_ = true; }
    position_history_.push_back(position);
    state_history_.push_back({board_, hands_});
    return true;
}

bool DobutsuEnv::act(const std::vector<std::string>& action_string_args)
{
    return act(DobutsuAction(action_string_args));
}

bool DobutsuEnv::isTerminal() const
{
    if (winner_ != Player::kPlayerNone || is_repetition_draw_) { return true; }
    // a side with no legal action cannot happen in dobutsu (a Lion always has a
    // move or has already been taken), but guard against it anyway
    for (int action_id = 0; action_id < kDobutsuPolicySize; ++action_id) {
        if (isLegalAction(DobutsuAction(action_id, turn_))) { return false; }
    }
    return true;
}

float DobutsuEnv::getEvalScore(bool is_resign) const
{
    Player result = is_resign ? getNextPlayer(turn_, kDobutsuNumPlayer) : winner_;
    switch (result) {
        case Player::kPlayer1: return 1.0f;
        case Player::kPlayer2: return -1.0f;
        default: return 0.0f;
    }
}

int DobutsuEnv::repetitionCountAt(int index) const
{
    return static_cast<int>(std::count(position_history_.begin(), position_history_.begin() + index + 1, position_history_[index]));
}

std::vector<float> DobutsuEnv::getFeatures(utils::Rotation rotation) const
{
    /*
       146 channels. For each of 8 history steps t (t = 0 is now), at t * 18:
          0~ 4. pieces of the side to move: giraffe, elephant, chick, hen, lion
          5~ 9. opponent pieces, same order
         10~11. repetition: plane 10 + (occurrences so far, capped at 2) - 1
         12~14. hand of the side to move: giraffe, elephant, chick
         15~17. opponent hand
       then
           144. 1 if Player1 is to move, else 0
           145. plies played / 512
       Every step is seen from the side to move now: for Player2 the board is
       turned 180 degrees, as in shogi.cpp.
    */
    const int area = kDobutsuBoardArea;
    Player opponent = getNextPlayer(turn_, kDobutsuNumPlayer);
    std::vector<float> features(kDobutsuNumInputChannels * area, 0.0f);

    for (int t = 0; t < kDobutsuHistory; ++t) {
        int index = static_cast<int>(state_history_.size()) - 1 - t;
        if (index < 0) { continue; }
        const int base = t * kDobutsuChannelsPerStep;
        const Board& board = state_history_[index].first;
        const GamePair<Hand>& hands = state_history_[index].second;

        for (int position = 0; position < area; ++position) {
            const Piece& piece = board[position];
            if (piece.isEmpty()) { continue; }
            int channel = base + static_cast<int>(piece.type_) + (piece.owner_ == turn_ ? 0 : kDobutsuNumPiecePlane);
            features[channel * area + relativeSquare(position, turn_)] = 1.0f;
        }

        int repetition = std::min(repetitionCountAt(index), kDobutsuNumRepetitionPlane);
        std::fill_n(features.begin() + (base + 2 * kDobutsuNumPiecePlane + repetition - 1) * area, area, 1.0f);

        const int hand_base = base + 2 * kDobutsuNumPiecePlane + kDobutsuNumRepetitionPlane;
        for (int i = 0; i < kDobutsuNumDroppable; ++i) {
            std::fill_n(features.begin() + (hand_base + i) * area, area, static_cast<float>(hands.get(turn_)[i]));
            std::fill_n(features.begin() + (hand_base + kDobutsuNumDroppable + i) * area, area, static_cast<float>(hands.get(opponent)[i]));
        }
    }

    const int global = kDobutsuHistory * kDobutsuChannelsPerStep;
    std::fill_n(features.begin() + global * area, area, turn_ == Player::kPlayer1 ? 1.0f : 0.0f);
    std::fill_n(features.begin() + (global + 1) * area, area, static_cast<float>(actions_.size()) / kDobutsuMoveCountScale);
    return features;
}

std::vector<float> DobutsuEnv::getActionFeatures(const DobutsuAction& action, utils::Rotation rotation) const
{
    return {};
}

std::string DobutsuEnv::toPositionString() const
{
    std::ostringstream oss;
    for (int position = 0; position < kDobutsuBoardArea; ++position) {
        const Piece& piece = board_[position];
        if (piece.isEmpty()) {
            oss << '.';
        } else {
            char letter = kPieceLetter[static_cast<int>(piece.type_)];
            oss << (piece.owner_ == Player::kPlayer1 ? letter : static_cast<char>(std::tolower(letter)));
        }
    }
    for (int i = 0; i < kDobutsuNumDroppable; ++i) { oss << hands_.get(Player::kPlayer1)[i]; }
    for (int i = 0; i < kDobutsuNumDroppable; ++i) { oss << hands_.get(Player::kPlayer2)[i]; }
    oss << playerToChar(turn_);
    return oss.str();
}

std::string DobutsuEnv::toString() const
{
    std::ostringstream oss;
    auto handString = [&](Player p) {
        std::string s;
        for (int i = 0; i < kDobutsuNumDroppable; ++i) {
            for (int n = 0; n < hands_.get(p)[i]; ++n) { s += kPieceLetter[i]; }
        }
        return s.empty() ? "-" : s;
    };
    oss << "W hand: " << handString(Player::kPlayer2) << "\n";
    oss << "   a b c\n";
    for (int row = 0; row < kDobutsuBoardHeight; ++row) {
        oss << " " << (kDobutsuBoardHeight - row) << " ";
        for (int col = 0; col < kDobutsuBoardWidth; ++col) {
            const Piece& piece = board_[toPosition(row, col)];
            if (piece.isEmpty()) {
                oss << ". ";
            } else {
                char letter = kPieceLetter[static_cast<int>(piece.type_)];
                oss << (piece.owner_ == Player::kPlayer1 ? letter : static_cast<char>(std::tolower(letter))) << " ";
            }
        }
        oss << "\n";
    }
    oss << "B hand: " << handString(Player::kPlayer1) << "\n";
    oss << "turn: " << playerToChar(turn_) << "\n";
    return oss.str();
}

std::vector<float> DobutsuEnvLoader::getActionFeatures(const int pos, utils::Rotation rotation) const
{
    return {};
}

} // namespace minizero::env::dobutsu
