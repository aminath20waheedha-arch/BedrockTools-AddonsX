#include "jumpscare.hpp"
#include "modules/ModuleRegistry.hpp"
#include <bedrocktools/events/EventBus.hpp>
#include <bedrocktools/sdk/Types.hpp>
#include <bedrocktools/sdk/Offsets.hpp>
#include <entt/entt.hpp>

// Reuses the same entity/component setup pattern as Keystrokes to read
// the jump (space) flag from MoveInputComponent every tick.

using uint = uint32_t;
using ushort = uint16_t;
using uchar = unsigned char;

enum class EntityId : uint32_t {};

template <size_t N, typename T>
struct bitset {
    T value;
    void set(size_t index, bool v) {
        if (v) value |= (1ULL << index);
        else value &= ~(1ULL << index);
    }
    bool test(size_t index) const {
        return (value & (1ULL << index)) != 0;
    }
};

struct EntityIdTraits {
    using value_type = EntityId;
    using entity_type = uint32_t;
    using version_type = uint16_t;
    static constexpr uint32_t entity_mask = 0x3FFFF;
    static constexpr uint32_t version_mask = 0x3FFF;
};

template<>
struct entt::entt_traits<EntityId> : entt::basic_entt_traits<EntityIdTraits> {
    static constexpr std::size_t page_size = ENTT_SPARSE_PAGE;
};

struct MoveInputState {
    bitset<27, uint> mFlagValues;
    bedrocktools::sdk::Vec2 mAnalogMoveVector;
    uchar mLookSlightDirField;
    uchar mLookNormalDirField;
    uchar mLookSmoothDirField;
    uchar pad[1];
};

struct MoveInputComponent {
    MoveInputState mInputState;
    MoveInputState mRawInputState;
    uchar mHoldAutoJumpInWaterTicks;
    uchar pad[3];
    bedrocktools::sdk::Vec2 mMove;
    bedrocktools::sdk::Vec2 mLookDelta;
    bedrocktools::sdk::Vec2 mInteractDir;
    bedrocktools::sdk::Vec3 mDisplacement;
    bedrocktools::sdk::Vec3 mDisplacementDelta;
    bedrocktools::sdk::Vec3 mCameraOrientation;
    bitset<11, ushort> mFlagValues;
    std::array<bool, 2> mIsPaddling;
};

class EntityRegistry;

class EntityContext {
public:
    inline entt::basic_registry<EntityId>& getRegistry() { return mEnTTRegistry; }

    template <class T>
    inline T* tryGetComponent() {
        return getRegistry().try_get<T>(mEntity);
    }

    EntityRegistry& mRegistry;
    entt::basic_registry<EntityId>& mEnTTRegistry;
    EntityId const mEntity;
};

static JumpscareModule* g_jumpscareMod = nullptr;

static void s_jumpscareTickCallback(void* _this) {
    if (!g_jumpscareMod || !g_jumpscareMod->enabled) return;

    EntityContext* ctx = reinterpret_cast<EntityContext*>(reinterpret_cast<char*>(_this) + bedrocktools::sdk::offsets::Actor::mEntityContext);
    if (!ctx) return;

    auto* moveInput = ctx->tryGetComponent<MoveInputComponent>();
    if (!moveInput) return;

    bool jumping = moveInput->mRawInputState.mFlagValues.test(7); // same bit Keystrokes uses for Space
    g_jumpscareMod->bSpace.store(jumping, std::memory_order_relaxed);
}

JumpscareModule::JumpscareModule()
    : Module("Jumpscare", "Flashes a scare on screen every time you jump.") {
    g_jumpscareMod = this;
}

JumpscareModule::~JumpscareModule() {
    if (g_jumpscareMod == this) g_jumpscareMod = nullptr;
}

void JumpscareModule::onInit() {
    bedrocktools::events::bus().subscribe<bedrocktools::events::LocalPlayerTickEvent>([](auto& event) { s_jumpscareTickCallback(event.player); });
}

void JumpscareModule::onEnable() {
    m_wasJumping = false;
    m_scareActive = false;
}

void JumpscareModule::onDisable() {
    m_scareActive = false;
}

void JumpscareModule::onFrame() {
    if (!enabled) return;

    bool jumping = bSpace.load(std::memory_order_relaxed);

    // trigger only on the moment you press jump, not every frame you're holding it
    if (jumping && !m_wasJumping) {
        m_scareActive = true;
        m_scareStart = std::chrono::steady_clock::now();
    }
    m_wasJumping = jumping;

    if (!m_scareActive) return;

    float elapsedMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - m_scareStart).count();

    if (elapsedMs > m_scareDurationMs) {
        m_scareActive = false;
        return;
    }

    std::vector<PLModMenu_DrawCommand> cmds;

    // full-screen red flash
    PLModMenu_DrawCommand flash = {};
    flash.type = PL_DRAW_RECT_FILLED;
    flash.x = 0;
    flash.y = 0;
    flash.w = 99999.0f; // draw system clips to screen bounds
    flash.h = 99999.0f;
    flash.color = 0xCCFF0000; // semi-transparent red
    cmds.push_back(std::move(flash));

    // giant "BOO" text centered-ish
    PLModMenu_DrawCommand text = {};
    text.type = PL_DRAW_TEXT;
    text.x = 200.0f;
    text.y = 200.0f;
    text.w = 400.0f;
    text.h = 150.0f;
    text.color = 0xFFFFFFFF;
    text.size = 120.0f;
    text.text = "BOO";
    cmds.push_back(std::move(text));

    submitDrawCommands(moduleId, cmds);
}
