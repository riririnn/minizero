#include "dobutsu.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace minizero::env::dobutsu {

namespace {

    inline int toPosition(int row, int col) { return row * kDobutsuBoardWidth + col; }
    inline int rowOf(int position) { return position / kDobutsuBoardWidth; }
    inline int colOf(int position) { return position % kDobutsuBoardWidth; }

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

} // namespace

std::string getDobutsuActionString(int action_id)
{
    if (action_id < 0 || action_id >= kDobutsuPolicySize) { return "??"; }
    // columns are letters a..c, rows are digits 1..4 counted from Player1's side
    auto squareString = [](int position) {
        std::string s;
        s += static_cast<char>('a' + colOf(position));
        s += static_cast<char>('1' + (kDobutsuBoardHeight - 1 - rowOf(position)));
        return s;
    };
    if (action_id >= kDobutsuBoardActionSize) {
        int id = action_id - kDobutsuBoardActionSize;
        std::string s;
        s += kPieceLetter[id / kDobutsuBoardArea];
        s += '*';
        s += squareString(id % kDobutsuBoardArea);
        return s;
    }
    int from = action_id / kDobutsuNumDirection, direction = action_id % kDobutsuNumDirection;
    int to_row = rowOf(from) + kDirectionRow[direction], to_col = colOf(from) + kDirectionCol[direction];
    if (to_row < 0 || to_row >= kDobutsuBoardHeight || to_col < 0 || to_col >= kDobutsuBoardWidth) { return "??"; }
    return squareString(from) + squareString(toPosition(to_row, to_col));
}

int getDobutsuActionID(const std::string& action_string)
{
    for (int action_id = 0; action_id < kDobutsuPolicySize; ++action_id) {
        if (getDobutsuActionString(action_id) == action_string) { return action_id; }
    }
    return -1;
}

void DobutsuEnv::reset()
{
    turn_ = Player::kPlayer1;
    winner_ = Player::kPlayerNone;
    is_repetition_draw_ = false;
    actions_.clear();
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

    position_history_.clear();
    position_history_.push_back(toPositionString());
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

int DobutsuEnv::findLion(Player player) const
{
    for (int position = 0; position < kDobutsuBoardArea; ++position) {
        if (!board_[position].isEmpty() && board_[position].owner_ == player &&
            board_[position].type_ == PieceType::kLion) {
            return position;
        }
    }
    return -1;
}

bool DobutsuEnv::isLegalAction(const DobutsuAction& action) const
{
    if (action.getPlayer() != turn_) { return false; }
    int action_id = action.getActionID();
    if (action_id < 0 || action_id >= kDobutsuPolicySize) { return false; }

    if (action.isDrop()) {
        PieceType type = action.getDropPieceType();
        if (hands_.get(turn_)[static_cast<int>(type)] == 0) { return false; }
        return board_[action.getDropPosition()].isEmpty();
    }

    int from = action.getFromPosition(), direction = action.getDirection();
    const Piece& piece = board_[from];
    if (piece.isEmpty() || piece.owner_ != turn_) { return false; }
    int to_row = rowOf(from) + kDirectionRow[direction], to_col = colOf(from) + kDirectionCol[direction];
    if (to_row < 0 || to_row >= kDobutsuBoardHeight || to_col < 0 || to_col >= kDobutsuBoardWidth) { return false; }
    int to = toPosition(to_row, to_col);
    if (!board_[to].isEmpty() && board_[to].owner_ == turn_) { return false; }
    return canReach(piece, from, to);
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

    if (action.isDrop()) {
        PieceType type = action.getDropPieceType();
        --hands_.get(turn_)[static_cast<int>(type)];
        board_[action.getDropPosition()] = {type, turn_};
    } else {
        int from = action.getFromPosition();
        int to = toPosition(rowOf(from) + kDirectionRow[action.getDirection()],
                            colOf(from) + kDirectionCol[action.getDirection()]);
        Piece moving = board_[from];
        const Piece& captured = board_[to];
        if (!captured.isEmpty()) {
            if (captured.type_ == PieceType::kLion) { winner_ = turn_; }
            // a captured Hen goes back to hand as a Chick; a Lion never does
            if (captured.type_ != PieceType::kLion) {
                PieceType held = (captured.type_ == PieceType::kHen ? PieceType::kChick : captured.type_);
                ++hands_.get(turn_)[static_cast<int>(held)];
            }
        }
        // a Chick reaching the far rank always becomes a Hen
        if (moving.type_ == PieceType::kChick && rowOf(to) == enemyBackRowOf(turn_)) { moving.type_ = PieceType::kHen; }
        board_[from] = Piece();
        board_[to] = moving;

        // walking the Lion in wins, unless the opponent can take it there
        if (winner_ == Player::kPlayerNone && moving.type_ == PieceType::kLion &&
            rowOf(to) == enemyBackRowOf(turn_) && !isAttacked(to, getNextPlayer(turn_, kDobutsuNumPlayer))) {
            winner_ = turn_;
        }
    }

    actions_.push_back(action);
    turn_ = action.nextPlayer();

    std::string position = toPositionString();
    if (std::count(position_history_.begin(), position_history_.end(), position) >= 2) { is_repetition_draw_ = true; }
    position_history_.push_back(position);
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

std::vector<float> DobutsuEnv::getFeatures(utils::Rotation rotation) const
{
    /*
       18 channels:
         0~ 4. own pieces by type (giraffe, elephant, chick, hen, lion)
         5~ 9. opponent pieces by type
        10~12. own hand count (giraffe, elephant, chick), spread over the plane
        13~15. opponent hand count
           16. black turn
           17. white turn
    */
    Player opponent = getNextPlayer(turn_, kDobutsuNumPlayer);
    std::vector<float> features(getNumInputChannels() * kDobutsuBoardArea, 0.0f);

    for (int position = 0; position < kDobutsuBoardArea; ++position) {
        const Piece& piece = board_[position];
        if (piece.isEmpty()) { continue; }
        int channel = static_cast<int>(piece.type_) + (piece.owner_ == turn_ ? 0 : 5);
        features[channel * kDobutsuBoardArea + position] = 1.0f;
    }
    for (int i = 0; i < kDobutsuNumDroppable; ++i) {
        float own = static_cast<float>(hands_.get(turn_)[i]);
        float opp = static_cast<float>(hands_.get(opponent)[i]);
        for (int position = 0; position < kDobutsuBoardArea; ++position) {
            features[(10 + i) * kDobutsuBoardArea + position] = own;
            features[(13 + i) * kDobutsuBoardArea + position] = opp;
        }
    }
    for (int position = 0; position < kDobutsuBoardArea; ++position) {
        features[16 * kDobutsuBoardArea + position] = static_cast<float>(turn_ == Player::kPlayer1);
        features[17 * kDobutsuBoardArea + position] = static_cast<float>(turn_ == Player::kPlayer2);
    }
    return features;
}

std::vector<float> DobutsuEnv::getActionFeatures(const DobutsuAction& action, utils::Rotation rotation) const
{
    throw std::runtime_error{"DobutsuEnv::getActionFeatures() is not implemented"};
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
    throw std::runtime_error{"DobutsuEnvLoader::getActionFeatures() is not implemented"};
}

} // namespace minizero::env::dobutsu
