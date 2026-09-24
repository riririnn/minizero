#pragma once

#include "base_env.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace minizero::env::minishogi {

// Minishogi (5五将棋, Shigenobu Kusumoto, c.1970): shogi on a 5x5 board with six
// pieces a side, built to the University of Electro-Communications tournament
// rules. The rules engine is our own; the network interface (features, move
// encoding, value) copies 9x9 shogi's conventions one for one, so architecture
// results on this board transfer to 9x9. See README.md in this directory.
const std::string kMinishogiName = "minishogi";
const int kMinishogiNumPlayer = 2;
const int kMinishogiBoardSize = 5;
const int kMinishogiBoardArea = kMinishogiBoardSize * kMinishogiBoardSize;

// move encoding, laid out like shogi.h's convertAZ: drops first, then
// from x (direction, promote). 9x9 has 7 drop types and 66 directions (two of
// them knight jumps); 5x5 has 5 drop types and 32 directions (no knight).
const int kMinishogiNumDroppable = 5;
const int kMinishogiMaxDistance = kMinishogiBoardSize - 1;                             // 4
const int kMinishogiNumMoveDirection = 8 * kMinishogiMaxDistance;                      // 32
const int kMinishogiMovePerSquare = kMinishogiNumMoveDirection * 2;                    // 64
const int kMinishogiDropActionSize = kMinishogiNumDroppable * kMinishogiBoardArea;     // 125
const int kMinishogiBoardActionSize = kMinishogiBoardArea * kMinishogiMovePerSquare;   // 1600
const int kMinishogiPolicySize = kMinishogiDropActionSize + kMinishogiBoardActionSize; // 1725

// features, laid out like shogi.cpp's getFeatures: 8 history steps of
// (own pieces, opponent pieces, repetition, own hand, opponent hand), then turn
// and move count. 9x9 is 8 x (14+14+3+7+7) + 2 = 362; 5x5 is 8 x (10+10+3+5+5) + 2.
const int kMinishogiHistory = 8;
const int kMinishogiNumPiecePlane = 10;
const int kMinishogiChannelsPerStep = 2 * kMinishogiNumPiecePlane + 3 + 2 * kMinishogiNumDroppable; // 33
const int kMinishogiNumInputChannels = kMinishogiHistory * kMinishogiChannelsPerStep + 2;           // 266
// the move-count plane is plies / 512 in shogi.cpp; the same constant keeps the
// plane meaning the same in both games
const float kMinishogiMoveCountScale = 512.0f;

// internal piece types. The order is the rules engine's own; the feature and
// drop orders that the network sees are separate tables in minishogi.cpp.
enum class PieceType {
    kPawn = 0,
    kSilver = 1,
    kGold = 2,
    kBishop = 3,
    kRook = 4,
    kPPawn = 5,   // と金
    kPSilver = 6, // 成銀
    kPGold = 7,   // never occurs; keeps the +kPromotionOffset arithmetic total
    kPBishop = 8, // 竜馬
    kPRook = 9,   // 竜王
    kKing = 10,
    kPieceTypeSize = 11,
};
const int kPromotionOffset = 5;
const int kNumHandSlot = 5; // hands are indexed by PieceType 0..4

// clockwise from north, in board coordinates (row 0 is Player2's back rank)
const std::array<int, 8> kDirectionRow = {-1, -1, 0, 1, 1, 1, 0, -1};
const std::array<int, 8> kDirectionCol = {0, 1, 1, 1, 0, -1, -1, -1};

// a move in board coordinates, decoded from an action id and its player
struct Move {
    bool valid_ = false;
    bool drop_ = false;
    PieceType drop_type_ = PieceType::kPieceTypeSize;
    int from_ = -1;
    int to_ = -1;
    bool promote_ = false;
};

Move decodeAction(int action_id, Player player);
int encodeMove(const Move& move, Player player);
std::string getMinishogiActionString(int action_id, Player player);
int getMinishogiActionID(const std::string& action_string, Player player);

class MinishogiAction : public BaseBoardAction<kMinishogiNumPlayer> {
public:
    MinishogiAction() : BaseBoardAction<kMinishogiNumPlayer>() {}
    MinishogiAction(int action_id, Player player) : BaseBoardAction<kMinishogiNumPlayer>(action_id, player) {}
    MinishogiAction(const std::vector<std::string>& action_string_args, int board_size = minizero::config::env_board_size)
    {
        assert(action_string_args.size() == 2);
        assert(action_string_args[0].size() == 1);
        player_ = charToPlayer(action_string_args[0][0]);
        assert(static_cast<int>(player_) > 0 && static_cast<int>(player_) <= kMinishogiNumPlayer);
        action_id_ = getMinishogiActionID(action_string_args[1], player_);
    }

    std::string toConsoleString() const override { return getMinishogiActionString(action_id_, player_); }
    inline Move decode() const { return decodeAction(action_id_, player_); }
};

// kPlayerNone as the owner means the square is empty
struct Piece {
    PieceType type_ = PieceType::kPieceTypeSize;
    Player owner_ = Player::kPlayerNone;
    inline bool isEmpty() const { return owner_ == Player::kPlayerNone; }
};

typedef std::array<Piece, kMinishogiBoardArea> Board;
typedef std::array<int, kNumHandSlot> Hand;

class MinishogiEnv : public BaseBoardEnv<MinishogiAction> {
public:
    MinishogiEnv() : BaseBoardEnv<MinishogiAction>(kMinishogiBoardSize) { reset(); }

    void reset() override;
    bool setFromSFEN(const std::string& sfen) override;
    bool act(const MinishogiAction& action) override;
    bool act(const std::vector<std::string>& action_string_args) override;
    std::vector<MinishogiAction> getLegalActions() const override;
    bool isLegalAction(const MinishogiAction& action) const override;
    bool isTerminal() const override;
    float getReward() const override { return 0.0f; }
    float getEvalScore(bool is_resign = false) const override;
    std::vector<float> getFeatures(utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    std::vector<float> getActionFeatures(const MinishogiAction& action, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    // getFeatures() rotates the board for White, so the value target -- and hence
    // the network's output -- is from the side to move. Convert back for MCTS.
    // The same override as shogi.h.
    float toFirstPlayerValue(float value) const override { return turn_ == Player::kPlayer2 ? -value : value; }
    std::string toString() const override;

    inline int getNumInputChannels() const override { return kMinishogiNumInputChannels; }
    inline int getNumActionFeatureChannels() const override { return 0; }
    inline int getPolicySize() const override { return kMinishogiPolicySize; }
    inline std::string name() const override { return kMinishogiName; }
    inline int getNumPlayer() const override { return kMinishogiNumPlayer; }
    // rotation is built into the features and the move encoding, as in shogi.h
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }

    inline const Board& getBoard() const { return board_; }
    inline const Hand& getHand(Player p) const { return hands_.get(p); }
    inline bool isInCheck(Player player) const { return isKingAttacked(board_, player); }

private:
    // the game ends the moment its result is known: mate, no legal move, the
    // fourth repetition or the move limit. Recorded there, not recomputed.
    Player winner_;
    bool is_draw_;
    Board board_;
    GamePair<Hand> hands_;
    // one entry per position, including the start; index i follows actions_[i-1]
    std::vector<std::string> position_history_;
    std::vector<std::pair<Board, GamePair<Hand>>> state_history_; // for the history planes
    std::vector<bool> check_history_;                             // did the move ending at this ply give check

    void startFrom(Player turn);
    bool isPseudoLegal(const Move& move, Player player) const;
    bool isUchifuzume(const Move& move) const;
    bool hasLegalAction() const;
    void applyMove(const Move& move, Player mover, Board& board, GamePair<Hand>& hands) const;
    bool isAttacked(const Board& board, int position, Player by) const;
    bool isKingAttacked(const Board& board, Player player) const;
    std::string toPositionString() const;
    int repetitionCountAt(int index) const;
    void settleGameEnd();
};

class MinishogiEnvLoader : public BaseBoardEnvLoader<MinishogiAction, MinishogiEnv> {
public:
    std::vector<float> getActionFeatures(const int pos, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    // value target from the side to move; draws are 0. The same as ShogiEnvLoader.
    inline std::vector<float> getValue(const int pos) const
    {
        return {getReturn() * (getTurnAt(pos) == Player::kPlayer1 ? 1.0f : -1.0f)};
    }
    inline Player getTurnAt(const int pos) const
    {
        if (action_pairs_.empty()) { return Player::kPlayer1; }
        if (pos < static_cast<int>(action_pairs_.size())) { return action_pairs_[pos].first.getPlayer(); }
        return getNextPlayer(action_pairs_.back().first.getPlayer(), kMinishogiNumPlayer);
    }
    inline std::string name() const override { return kMinishogiName; }
    inline int getPolicySize() const override { return kMinishogiPolicySize; }
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }
};

} // namespace minizero::env::minishogi
