#pragma once

#include "core/TextEditBuffer.h"
#include "game/Localization.h"
#include "ui/ContainerScreen.h"
#include "ui/Hotbar.h"
#include "ui/Inventory.h"
#include "ui/Menu.h"
#include "ui/SurvivalInventory.h"
#include "ui/UIRenderer.h"

#include <deque>
#include <memory>
#include <string>

class IGameRenderer;
class ApplicationInputController;
class ClientSettings;
class GameSession;
class Player;
class Window;
namespace platform { class Clipboard; }

class GameUiController {
public:
    GameUiController(Player& player, platform::Clipboard& clipboard);

    void tick(float dt);
    void showMessage(const std::string& message);
    void openCommand();
    void openInventory(bool creativeCatalog);
    void openCreativeCatalog();
    void openPlayerInventoryTab();
    void closeCommand();
    bool playerInventoryViewOpen(const Player& player) const;
    void render(GameSession& session, const ClientSettings& settings,
                ApplicationInputController& inputs, Window& window,
                GameState state, bool showCrosshair);

    UIRenderer renderer;
    Localization localization;
    std::unique_ptr<Menu> activeMenu;
    MenuCallbacks menuCallbacks;
    Hotbar hotbar;
    CreativeInventory inventory;
    SurvivalInventoryScreen survivalInventory;
    ContainerScreen containerScreen;
    bool containerOpen = false;
    bool inventoryOpen = false;
    bool creativeCatalogOpen = true;
    double mouseScreenX = 0.0;
    double mouseScreenY = 0.0;
    bool commandOpen = false;
    TextEditBuffer commandInput;
    std::deque<std::string> chatHistory;
    float chatVisibleSeconds = 0.0f;
    int guiScale = 1;
    int lastHudSlot = -1;
    float itemNameSeconds = 0.0f;

private:
    void renderSurvivalHud(const Player& player, int screenWidth);
    void renderSelectedItemName(const Player& player, int screenWidth);
    void renderCrosshairAndMiningProgress(const Player& player,
                                          int screenWidth, int screenHeight);
};
