#pragma once

#include "base_env.h"
#include <array>
#include <string>
#include <vector>

namespace minizero::env::dobutsu {

// Dobutsu shogi (どうぶつしょうぎ, Madoka Kitao): shogi on a 3x4 board with four
// pieces a side. See README.md in this directory for the rules and the design.
const std::string kDobutsuName = "dobutsu";
const int kDobutsuNumPlayer = 2;
const int kDobutsuBoardWidth = 3;
const int kDobutsuBoardHeight = 4;
const int kDobutsuBoardArea = kDobutsuBoardWidth * kDobutsuBoardHeight;

// board moves are (square, direction), drops are (piece, square)
const int kDobutsuNumDirection = 8;
const int kDobutsuNumDroppable = 3; // giraffe, elephant, chick
const int kDobutsuBoardActionSize = kDobutsuBoardArea * kDobutsuNumDirection;
const int kDobutsuDropActionSize = kDobutsuNumDroppable * kDobutsuBoardArea;
const int kDobutsuPolicySize = kDobutsuBoardActionSize + kDobutsuDropActionSize;

// the droppable pieces come first so a hand slot maps straight onto a drop action
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

std::string getDobutsuActionString(int action_id);
int getDobutsuActionID(const std::string& action_string);

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
        action_id_ = getDobutsuActionID(action_string_args[1]);
    }

    std::string toConsoleString() const override { return getDobutsuActionString(getActionID()); }

    inline bool isDrop() const { return action_id_ >= kDobutsuBoardActionSize; }
    inline int getFromPosition() const { return action_id_ / kDobutsuNumDirection; }
    inline int getDirection() const { return action_id_ % kDobutsuNumDirection; }
    inline PieceType getDropPieceType() const { return static_cast<PieceType>((action_id_ - kDobutsuBoardActionSize) / kDobutsuBoardArea); }
    inline int getDropPosition() const { return (action_id_ - kDobutsuBoardActionSize) % kDobutsuBoardArea; }
};

// kPlayerNone as the owner means the square is empty
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
    bool act(const DobutsuAction& action) override;
    bool act(const std::vector<std::string>& action_string_args) override;
    std::vector<DobutsuAction> getLegalActions() const override;
    bool isLegalAction(const DobutsuAction& action) const override;
    bool isTerminal() const override;
    float getReward() const override { return 0.0f; }
    float getEvalScore(bool is_resign = false) const override;
    std::vector<float> getFeatures(utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    std::vector<float> getActionFeatures(const DobutsuAction& action, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    std::string toString() const override;

    // 5 piece types x own/opponent on the board, 3 hand types x own/opponent
    // spread over the plane, and one plane per player for the turn
    inline int getNumInputChannels() const override { return 18; }
    inline int getNumActionFeatureChannels() const override { return 1; }
    inline int getInputChannelHeight() const override { return kDobutsuBoardHeight; }
    inline int getInputChannelWidth() const override { return kDobutsuBoardWidth; }
    inline int getHiddenChannelHeight() const override { return kDobutsuBoardHeight; }
    inline int getHiddenChannelWidth() const override { return kDobutsuBoardWidth; }
    inline int getPolicySize() const override { return kDobutsuPolicySize; }
    inline std::string name() const override { return kDobutsuName; }
    inline int getNumPlayer() const override { return kDobutsuNumPlayer; }
    // the board is never rotated, so positions and actions stay as they are
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }

    inline const Board& getBoard() const { return board_; }
    inline const Hand& getHand(Player p) const { return hands_.get(p); }

private:
    // a Lion is captured or walks in the moment the game ends, so the winner is
    // recorded there rather than recomputed from the board
    Player winner_;
    bool is_repetition_draw_;
    Board board_;
    GamePair<Hand> hands_;
    std::vector<std::string> position_history_; // for the three-fold repetition draw

    bool canReach(const Piece& piece, int from, int to) const;
    bool isAttacked(int position, Player by) const;
    int findLion(Player player) const;
    std::string toPositionString() const;
};

class DobutsuEnvLoader : public BaseBoardEnvLoader<DobutsuAction, DobutsuEnv> {
public:
    std::vector<float> getActionFeatures(const int pos, utils::Rotation rotation = utils::Rotation::kRotationNone) const override;
    inline std::vector<float> getValue(const int pos) const { return {getReturn()}; }
    inline std::string name() const override { return kDobutsuName; }
    inline int getPolicySize() const override { return kDobutsuPolicySize; }
    inline int getRotatePosition(int position, utils::Rotation rotation) const override { return position; }
    inline int getRotateAction(int action_id, utils::Rotation rotation) const override { return action_id; }
};

} // namespace minizero::env::dobutsu
