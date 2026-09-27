#pragma once

#include "base_env.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

namespace minizero::env::dobutsu {

// Dobutsu shogi (どうぶつしょうぎ, Madoka Kitao): shogi on a 3x4 board.
// The network interface follows 9x9 shogi and minishogi; see README.md.
const std::string kDobutsuName = "dobutsu";
const int kDobutsuNumPlayer = 2;
const int kDobutsuBoardWidth = 3;
const int kDobutsuBoardHeight = 4;
const int kDobutsuBoardArea = kDobutsuBoardWidth * kDobutsuBoardHeight;

const int kDobutsuNumDirection = 8;
const int kDobutsuNumDroppable = 3;
const int kDobutsuDropActionSize = kDobutsuNumDroppable * kDobutsuBoardArea;     // 36
const int kDobutsuBoardActionSize = kDobutsuBoardArea * kDobutsuNumDirection;    // 96
const int kDobutsuPolicySize = kDobutsuDropActionSize + kDobutsuBoardActionSize; // 132

const int kDobutsuHistory = 8;
const int kDobutsuNumPiecePlane = 5;
const int kDobutsuNumRepetitionPlane = 2;                                                                              // the third occurrence ends the game
const int kDobutsuChannelsPerStep = 2 * kDobutsuNumPiecePlane + kDobutsuNumRepetitionPlane + 2 * kDobutsuNumDroppable; // 18
const int kDobutsuNumInputChannels = kDobutsuHistory * kDobutsuChannelsPerStep + 2;                                    // 146
const float kDobutsuMoveCountScale = 512.0f;

enum class PieceType {
    kGiraffe = 0,
    kElephant = 1,
    kChick = 2,
    kHen = 3,
    kLion = 4,
    kPieceTypeSize = 5,
};

// clockwise from north, in board coordinates (row 0 is Player2's back rank)
const std::array<int, kDobutsuNumDirection> kDirectionRow = {-1, -1, 0, 1, 1, 1, 0, -1};
const std::array<int, kDobutsuNumDirection> kDirectionCol = {0, 1, 1, 1, 0, -1, -1, -1};

struct Move {
    bool valid_ = false;
    bool drop_ = false;
    PieceType drop_type_ = PieceType::kPieceTypeSize;
    int from_ = -1;
    int to_ = -1;
};

Move decodeAction(int action_id, Player player);
int encodeMove(const Move& move, Player player);
std::string getDobutsuActionString(int action_id, Player player);
int getDobutsuActionID(const std::string& action_string, Player player);

class DobutsuAction : public BaseBoardAction<kDobutsuNumPlayer> {
public:
    DobutsuAction() : BaseBoardAction<kDobutsuNumPlayer>() {}
    DobutsuAction(int action_id, Player player) : BaseBoardAction<kDobutsuNumPlayer>(action_id, player) {}
    DobutsuAction(const std::vector<std::string>& action_string_args, int board_size = minizero::config::env_board_size)
    {
        assert(action_string_args.size() == 2);
        assert(action_string_args[0].size() == 1);
        player_ = charToPlayer(action_string_args[0][0]);
        assert(static_cast<int>(player_) > 0 && static_cast<int>(player_) <= kDobutsuNumPlayer);
        action_id_ = getDobutsuActionID(action_string_args[1], player_);
    }

    std::string toConsoleString() const override { return getDobutsuActionString(action_id_, player_); }
    inline Move decode() const { return decodeAction(action_id_, player_); }
};

struct Piece {
    PieceType type_ = PieceType::kPieceTypeSize;
    Player owner_ = Player::kPlayerNone;
    inline bool isEmpty() const { return owner_ == Player::kPlayerNone; }
};

typedef std::array<Piece, kDobutsuBoardArea> Board;
typedef std::array<int, kDobutsuNumDroppable> Hand;

class DobutsuEnv : public BaseBoardEnv<DobutsuAction> {
public:
    DobutsuEnv() : BaseBoardEnv<DobutsuAction>(kDobutsuBoardWidth) { reset(); }

    void reset() override;
    bool setFromSFEN(const std::string& sfen) override;
    bool act(const DobutsuAction& action) override;
    bool act(const std::vector<std::string>& action_string_args) override;
    std::vector<DobutsuAction> getLegalActions() const override;
    bool isLegalAction(const DobutsuAction& action) const override;
    bool isTerminal() const override;
    float getReward() const override { return 0.0f; }
    float getEvalScore(bool is_resign = false) const override;
    std::vector<float> getFeatures(utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    std::vector<float> getActionFeatures(const DobutsuAction& action, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    // the board is rotated for White, so the value is from the side to move
    float toFirstPlayerValue(float value) const override { return turn_ == Player::kPlayer2 ? -value : value; }
    std::string toString() const override;

    inline int getNumInputChannels() const override { return kDobutsuNumInputChannels; }
    inline int getNumActionFeatureChannels() const override { return 0; }
    inline int getInputChannelHeight() const override { return kDobutsuBoardHeight; }
    inline int getInputChannelWidth() const override { return kDobutsuBoardWidth; }
    inline int getHiddenChannelHeight() const override { return kDobutsuBoardHeight; }
    inline int getHiddenChannelWidth() const override { return kDobutsuBoardWidth; }
    inline int getPolicySize() const override { return kDobutsuPolicySize; }
    inline std::string name() const override { return kDobutsuName; }
    inline int getNumPlayer() const override { return kDobutsuNumPlayer; }
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }

    inline const Board& getBoard() const { return board_; }
    inline const Hand& getHand(Player p) const { return hands_.get(p); }

private:
    Player winner_;
    bool is_repetition_draw_;
    Board board_;
    GamePair<Hand> hands_;
    std::vector<std::string> position_history_;
    std::vector<std::pair<Board, GamePair<Hand>>> state_history_;

    void startFrom(Player turn);
    bool canReach(const Piece& piece, int from, int to) const;
    bool isAttacked(int position, Player by) const;
    std::string toPositionString() const;
    int repetitionCountAt(int index) const;
};

class DobutsuEnvLoader : public BaseBoardEnvLoader<DobutsuAction, DobutsuEnv> {
public:
    std::vector<float> getActionFeatures(const int pos, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    inline std::vector<float> getValue(const int pos) const
    {
        return {getReturn() * (getTurnAt(pos) == Player::kPlayer1 ? 1.0f : -1.0f)};
    }
    inline Player getTurnAt(const int pos) const
    {
        if (action_pairs_.empty()) { return Player::kPlayer1; }
        if (pos < static_cast<int>(action_pairs_.size())) { return action_pairs_[pos].first.getPlayer(); }
        return getNextPlayer(action_pairs_.back().first.getPlayer(), kDobutsuNumPlayer);
    }
    inline std::string name() const override { return kDobutsuName; }
    inline int getPolicySize() const override { return kDobutsuPolicySize; }
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }
};

} // namespace minizero::env::dobutsu
