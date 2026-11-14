#include <API/ARK/Ark.h>
#include <ICommands.h>
#include <Ark/ArkApiUtils.h>

#include <chrono>
#include <cstdint>
#include <cwctype>
#include <iterator>
#include <string>
#include <unordered_map>

// NOBS Tribe Invite 1.0.0 - ASA API 2.11
// Deliberately does not merge tribes or force membership.
// Designed for a single map/server process; each map runs its own DLL.
namespace
{
    using Clock = std::chrono::steady_clock;
    constexpr int kMaxMembers = 6;
    constexpr auto kInviteLifetime = std::chrono::minutes(2);
    constexpr auto kInviteCooldown = std::chrono::seconds(15);
    constexpr wchar_t kSender[] = L"NOBS Tribe Invite";

    struct Invitation
    {
        uint64_t inviterId;
        uint64_t targetId;
        int tribeId;
        int targetOriginalTribeId;
        Clock::time_point expiresAt;
    };
    std::unordered_map<uint64_t, Invitation> pending;
    std::unordered_map<uint64_t, Clock::time_point> lastInvite;

    void Tell(AShooterPlayerController* pc, const wchar_t* message)
    {
        if (pc)
            AsaApi::GetApiUtils().SendChatMessage(pc, FString(kSender), message);
    }

    uint64_t ID(AShooterPlayerController* pc)
    {
        return pc ? static_cast<uint64_t>(pc->LinkedPlayerIDField()) : 0;
    }

    AShooterPlayerState* State(AShooterPlayerController* pc)
    {
        if (!pc) return nullptr;
        return static_cast<AShooterPlayerState*>(pc->PlayerStateField().Get());
    }

    bool Active(AShooterPlayerController* pc)
    {
        return pc && ID(pc) != 0 && State(pc) != nullptr &&
               pc->GetPlayerCharacter() && !pc->GetPlayerCharacter()->IsDead();
    }

    AShooterPlayerController* OnlineByID(uint64_t playerId)
    {
        auto* world = AsaApi::GetApiUtils().GetWorld();
        if (!world) return nullptr;
        for (auto weak : world->PlayerControllerListField())
        {
            auto* pc = static_cast<AShooterPlayerController*>(weak.Get());
            if (pc && ID(pc) == playerId)
                return pc;
        }
        return nullptr;
    }

    std::wstring Trim(std::wstring s)
    {
        size_t start = 0;
        while (start < s.size() && std::iswspace(s[start])) ++start;
        size_t end = s.size();
        while (end > start && std::iswspace(s[end - 1])) --end;
        return s.substr(start, end - start);
    }

    std::wstring Argument(FString* input)
    {
        if (!input) return L"";
        std::wstring text = Trim(std::wstring(**input));
        auto space = text.find_first_of(L" \t\r\n");
        if (space == std::wstring::npos) return L"";
        return Trim(text.substr(space + 1));
    }

    bool CanInvite(AShooterPlayerController* inviter, int* outTribeId = nullptr)
    {
        auto* state = State(inviter);
        if (!state || !state->IsInTribe()) return false;
        auto& tribe = state->MyTribeDataField();
        int tribeId = state->GetTribeId();
        if (tribeId <= 0 || tribe.TribeIDField() != tribeId) return false;
        const auto id = static_cast<unsigned int>(ID(inviter));
        const bool authorized = tribe.OwnerPlayerDataIDField() == id ||
            tribe.TribeAdminsField().Contains(id);
        if (!authorized) return false;
        if (outTribeId) *outTribeId = tribeId;
        return true;
    }

    bool SoleMemberOfOldTribe(AShooterPlayerState* targetState, uint64_t targetId)
    {
        if (!targetState || !targetState->IsInTribe()) return true;
        auto& old = targetState->MyTribeDataField();
        const auto& members = old.MembersPlayerDataIDField();
        return members.Num() == 1 && members.Contains(static_cast<unsigned int>(targetId)) &&
            old.OwnerPlayerDataIDField() == static_cast<unsigned int>(targetId);
    }

    void PurgeExpired()
    {
        const auto now = Clock::now();
        for (auto it = pending.begin(); it != pending.end();)
            it = (it->second.expiresAt <= now) ? pending.erase(it) : std::next(it);
    }

    void OnInvite(AShooterPlayerController* inviter, FString* message, int, int)
    {
        if (!Active(inviter)) return;
        PurgeExpired();
        int tribeId = 0;
        if (!CanInvite(inviter, &tribeId))
        {
            Tell(inviter, L"Only your tribe owner or admin can invite players.");
            return;
        }
        auto* inviterState = State(inviter);
        if (inviterState->MyTribeDataField().MembersPlayerDataIDField().Num() >= kMaxMembers)
        {
            Tell(inviter, L"Your tribe already has six members.");
            return;
        }
        auto targetName = Argument(message);
        if (targetName.empty())
        {
            Tell(inviter, L"Usage: /tribeup ExactCharacterName (spaces allowed)");
            return;
        }
        const auto myId = ID(inviter);
        const auto now = Clock::now();
        auto cooldown = lastInvite.find(myId);
        if (cooldown != lastInvite.end() && now - cooldown->second < kInviteCooldown)
        {
            Tell(inviter, L"Wait 15 seconds between invitations.");
            return;
        }
        auto matches = AsaApi::GetApiUtils().FindPlayerFromCharacterName(
            FString(targetName.c_str()), ESearchCase::IgnoreCase, true);
        if (matches.Num() != 1)
        {
            Tell(inviter, matches.Num() == 0
                ? L"No online survivor has that exact character name."
                : L"Duplicate survivor names: use an unambiguous name.");
            return;
        }
        auto* target = matches[0];
        if (!Active(target) || ID(target) == myId)
        {
            Tell(inviter, L"That survivor cannot be invited right now.");
            return;
        }
        auto* targetState = State(target);
        if (targetState->IsInTribe() && targetState->GetTribeId() == tribeId)
        {
            Tell(inviter, L"This survivor already belongs to your tribe.");
            return;
        }
        if (!SoleMemberOfOldTribe(targetState, ID(target)))
        {
            Tell(inviter, L"Target belongs to another multi-member tribe. They must leave it normally first.");
            return;
        }
        const int originalTargetTribeId = targetState->IsInTribe() ? targetState->GetTribeId() : 0;
        pending[ID(target)] = Invitation{myId, ID(target), tribeId, originalTargetTribeId, now + kInviteLifetime};
        lastInvite[myId] = now;
        Tell(inviter, L"Invitation sent. It expires in two minutes.");
        Tell(target, L"A tribe owner/admin invited you! Type /tribeaccept or /tribedecline within 2 minutes.");
        if (targetState->IsInTribe() && targetState->GetTribeId() != tribeId)
            Tell(target, L"IMPORTANT: You own a separate solo tribe. Joining won't merge its dinos/structures. Use /tribeaccept confirm if you want to proceed.");
    }

    void OnAccept(AShooterPlayerController* target, FString* message, int, int)
    {
        if (!Active(target)) return;
        PurgeExpired();
        const auto myId = ID(target);
        auto it = pending.find(myId);
        if (it == pending.end())
        {
            Tell(target, L"No pending invitation. Ask a tribe owner to use /tribeup YourName.");
            return;
        }
        Invitation invitation = it->second;
        if (invitation.expiresAt <= Clock::now())
        {
            pending.erase(it);
            Tell(target, L"Your invitation expired. Request another.");
            return;
        }
        auto* inviter = OnlineByID(invitation.inviterId);
        int liveTribeId = 0;
        if (!Active(inviter) || !CanInvite(inviter, &liveTribeId) || liveTribeId != invitation.tribeId)
        {
            pending.erase(it);
            Tell(target, L"Invite is no longer valid: inviter is offline or cannot invite.");
            return;
        }
        auto* inviterState = State(inviter);
        auto* targetState = State(target);
        if (!inviterState || !targetState) return;
        const int currentTargetTribeId = targetState->IsInTribe() ? targetState->GetTribeId() : 0;
        if (currentTargetTribeId != invitation.targetOriginalTribeId)
        {
            Tell(target, L"Your tribe membership changed since this invitation was issued. Request a new invitation.");
            pending.erase(it);
            return;
        }
        auto& tribe = inviterState->MyTribeDataField();
        if (tribe.MembersPlayerDataIDField().Num() >= kMaxMembers)
        {
            Tell(target, L"The target tribe is now full (6/6).");
            pending.erase(it);
            return;
        }
        if (targetState->IsInTribe() && targetState->GetTribeId() == liveTribeId)
        {
            Tell(target, L"You are already in that tribe.");
            pending.erase(it);
            return;
        }
        if (!SoleMemberOfOldTribe(targetState, myId))
        {
            Tell(target, L"You must leave your current multi-member tribe first.");
            pending.erase(it);
            return;
        }
        const bool ownsOldSoloTribe = targetState->IsInTribe() &&
            targetState->GetTribeId() > 0 && targetState->GetTribeId() != liveTribeId;
        if (ownsOldSoloTribe && Argument(message) != L"confirm")
        {
            Tell(target, L"CAUTION: Former tribe structures/dinos are NOT merged. Type /tribeaccept confirm to join, or /tribedecline.");
            return;
        }
        auto* gameMode = AsaApi::GetApiUtils().GetShooterGameMode();
        if (!gameMode || !gameMode->AllowAddToTribe(targetState, &tribe))
        {
            Tell(target, L"ARK rejected the tribe-join eligibility check. No changes made.");
            return;
        }
        // Native join path, never tribe merge, never force.
        const bool joined = targetState->AddToTribe(&tribe, false, false, true, inviter);
        if (!joined || targetState->GetTribeId() != liveTribeId)
        {
            Tell(target, L"ARK did not confirm the new tribe membership. Check tribe ID and server logs.");
            return;
        }
        pending.erase(it);
        Tell(target, L"Joined the invited tribe successfully.");
        Tell(inviter, L"A player accepted your tribe invitation.");
        Log::GetLog()->info("[NOBSTribeInvite] Successful tribe join: inviterPlayerId={} targetPlayerId={} tribeId={}",
            invitation.inviterId, myId, liveTribeId);
    }

    void OnDecline(AShooterPlayerController* target, FString*, int, int)
    {
        if (!target) return;
        const auto id = ID(target);
        auto it = pending.find(id);
        if (it == pending.end())
        {
            Tell(target, L"No pending invitation.");
            return;
        }
        auto* inviter = OnlineByID(it->second.inviterId);
        pending.erase(it);
        Tell(target, L"Invitation declined.");
        Tell(inviter, L"Your tribe invitation was declined.");
    }
}

extern "C" __declspec(dllexport) void Plugin_Init()
{
    Log::Get().Init("NOBSTribeInvite");
    auto& commands = AsaApi::GetCommands();
    commands.AddChatCommand(L"/tribeup", &OnInvite);
    commands.AddChatCommand(L"/tribeaccept", &OnAccept);
    commands.AddChatCommand(L"/tribedecline", &OnDecline);
    Log::GetLog()->info("[NOBSTribeInvite] v1.0.0 registered /tribeup, /tribeaccept, /tribedecline");
}

extern "C" __declspec(dllexport) void Plugin_Unload()
{
    auto& commands = AsaApi::GetCommands();
    commands.RemoveChatCommand(L"/tribeup");
    commands.RemoveChatCommand(L"/tribeaccept");
    commands.RemoveChatCommand(L"/tribedecline");
    pending.clear();
    lastInvite.clear();
}
