#include "EmblemEffect.h"
#include "EmblemScope.h"

#include "Changer/Items/AssetPrecache.h"
#include "Changer/Items/FakeLoadout.h"
#include "Changer/Match/NetSkins.h"
#include "Changer/Match/ImpactEffects.h"
#include "Changer/Match/ParticleReplace.h"
#include "Changer/Inventory/FakeItems.h"
#include "SDK/Econ/CEconItemSchema.h"
#include "SDK/Econ/C_EconItemView.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTAPlayerController.h"
#include "SDK/EntitySystem/EntityUtils/C_DOTA_BaseNPC_Hero.h"
#include "SDK/EntitySystem/CGameEntitySystem.h"
#include "SDK/EntitySystem/EntityUtils/C_BaseEntity.h"
#include "SDK/Logger/Logger.h"
#include "SDK/NetDelta/ResourceId.h"
#include "SDK/Network/CNetMessage.h"
#include "SDK/Protobuf/CProtobufMessage.h"
#include "SDK/ResourceSystem/CResourceSystem.h"

#include "dota_usermessages.pb.h"

#include <atomic>
#include <type_traits>
#include <mutex>
#include <string>

namespace
{
    constexpr const char* EmblemType = "diretide_effect";
    constexpr const char* EmblemAsset = "emblem_attack_effect";
    constexpr std::uint32_t NetworkIndexMask = 0x3FFF;
    constexpr int ServerAdditionalLogLimit = 200;

    std::mutex PathMutex;
    std::string AttackPath;
    std::atomic<int> ServerAdditionalLogs{0};

    std::string CurrentPath()
    {
        std::lock_guard<std::mutex> Lock(PathMutex);
        return AttackPath;
    }

    template <typename T>
    void Apply(CNetMessage* Message)
    {
        CProtobufMessage* Proto = Message->Proto();
        T Projectile;
        if (!Proto || !Proto->CopyTo(Projectile))
            return;

        auto* LocalHero = C_DOTAPlayerController::LocalHero();
        const int LocalIndex = LocalHero ? LocalHero->Index() : -1;
        bool Changed = false;
        int Source = static_cast<int>(Projectile.source() & NetworkIndexMask);
        std::uint64_t Particle = static_cast<std::uint64_t>(Projectile.particle_system_handle());
        CGameEntitySystem* Entities = CGameEntitySystem::Get();
        C_BaseEntity* SourceEntity = Entities ? Entities->Entity(Source) : nullptr;
        bool Swapped = Projectile.is_attack() && SourceEntity && NetSkins::IsSwapped(Source, SourceEntity->ModelId());
        if (Swapped && ParticleReplace::Replace(Particle))
        {
            Projectile.set_particle_system_handle(static_cast<std::int64_t>(Particle));
            Changed = true;
        }
        if constexpr (std::is_same_v<T, CDOTAUserMsg_TE_Projectile>)
        {
            if (Swapped)
                ImpactEffects::OnAttackProjectile(Source, Projectile.handle(), Projectile.target());
        }

        if (Projectile.has_additional_particle_system_handle() && Projectile.additional_particle_system_handle())
        {
            if (ServerAdditionalLogs.fetch_add(1) < ServerAdditionalLogLimit)
            {
                CResourceSystem* Resources = CResourceSystem::Get();
                const char* Name = Resources ? Resources->Name(static_cast<std::uint64_t>(Projectile.additional_particle_system_handle())) : nullptr;
                Log("Emblem server additional %s source %u", Name ? Name : "?", Projectile.source());
            }
        }
        else if (LocalIndex >= 0 && Projectile.has_source() && static_cast<int>(Projectile.source() & NetworkIndexMask) == LocalIndex && Projectile.is_attack())
        {
            std::string Path = CurrentPath();
            if (!Path.empty())
            {
                AssetPrecache::Ensure(Path);
                Changed |= EmblemScope::AddAttack(Projectile, LocalIndex, NetDelta::ResourceId(Path.c_str()));
            }
        }

        if (!Changed)
            return;
        if (!Proto->CopyFrom(Projectile))
            LogE("Projectile write back failed");
    }
}

void EmblemEffect::Update()
{
    std::string Path;
    auto* Schema = CEconItemSchema::Get();
    for (const auto& Item : FakeItems::Equipped())
    {
        bool Equipped = false;
        for (const auto& State : Item.equipped_state())
            if ((State.new_class() == 1000 || State.new_class() == 0) && State.new_slot() == 13) Equipped = true;
        if (!Equipped || !Schema) continue;
        auto* Def = Schema->GetItemDefinition(Item.def_index()); if (!Def) continue;
        const auto Modifiers = Def->AssetModifiers(Item.style() == 255 ? 0 : Item.style());
        Path = FakeLoadout::Find(Modifiers, EmblemType, EmblemAsset);
    }
    std::lock_guard<std::mutex> Lock(PathMutex);
    if (Path != AttackPath)
        Log("Emblem attack effect %s", Path.empty() ? "none" : Path.c_str());
    AttackPath = std::move(Path);
}

bool EmblemEffect::IsLocalEmblem(const C_EconItemView* View)
{
    if (!View || !FakeItems::IsFake(View->ItemId())) return false;
    const auto Item = FakeItems::Find(View->ItemId()); if (!Item) return false;
    for (const auto& State : Item->equipped_state())
        if ((State.new_class() == 1000 || State.new_class() == 0) && State.new_slot() == 13) return true;
    return false;
}

void EmblemEffect::OnProjectile(CNetMessage* Message, bool Located)
{
    if (!Message)
        return;
    if (Located)
        Apply<CDOTAUserMsg_TE_ProjectileLoc>(Message);
    else
        Apply<CDOTAUserMsg_TE_Projectile>(Message);
}
