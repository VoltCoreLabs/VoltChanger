#include "Panorama.h"

#include "SDK/Logger/Logger.h"
#include "SDK/Panorama/UIEngine.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
    struct Binding
    {
        Panorama::Handler Callback;
        std::intptr_t Argument;
        Panorama::RootKind Kind;
    };

    std::vector<std::unique_ptr<Binding>> Bindings;
    V8::Address* RootContexts[2] = {};
    V8::Address* RootPanels[2] = {};
    V8::Address* CurrentRoot = nullptr;
    Panorama::RootKind CurrentKind = Panorama::RootKind::Dashboard;
    int ReportedExceptions = 0;
    bool ReportedRoots = false;
    void* CurrentIsolate = nullptr;
    V8::Local CurrentContext = nullptr;

    void Trampoline(const void* Info)
    {
        auto* Bound = static_cast<Binding*>(V8::CallbackData(Info));
        if (!Bound)
        {
            LogE("Panorama event: callback data missing");
            return;
        }

        Panorama::Scope Scope(Bound->Kind);
        if (Scope.IsValid())
            Bound->Callback(Bound->Argument);
    }


    std::string ReadString(void* Isolate, V8::Local Context, V8::Local Object, const char* Key)
    {
        V8::Local Value = V8::Get(Isolate, Object, Context, Key);
        return Value ? V8::ToString(Isolate, Value) : std::string();
    }

    V8::Local FindRoot(void* Isolate, V8::Local Context, int& Depth)
    {
        Depth = -1;
        V8::Local Global = V8::ContextGlobal(Context);
        V8::Local Dollar = V8::Get(Isolate, Global, Context, "$");
        if (!Dollar || !V8::IsObject(Dollar))
            return nullptr;

        V8::Local Getter = V8::Get(Isolate, Dollar, Context, "GetContextPanel");
        if (!Getter || !V8::IsFunction(Getter))
            return nullptr;

        V8::Local Panel = V8::Call(Getter, Context, Dollar, 0, nullptr);
        if (!Panel || !V8::IsObject(Panel))
            return nullptr;

        Depth = 0;
        while (true)
        {
            V8::Local Parent = V8::Get(Isolate, Panel, Context, "GetParent");
            V8::Local Next = Parent && V8::IsFunction(Parent) ? V8::Call(Parent, Context, Panel, 0, nullptr) : nullptr;
            if (!Next || !V8::IsObject(Next))
                return Panel;
            Panel = Next;
            ++Depth;
        }
    }

    bool SelectRootContext(void* Isolate, const UIEngine* Engine, Panorama::RootKind Kind)
    {
        const int SlotIndex = static_cast<int>(Kind);
        const char* RootPanelId = Kind == Panorama::RootKind::Hud ? "DotaHud" : "DotaDashboard";
        V8::Local BestContext = nullptr;
        V8::Local BestRoot = nullptr;
        int BestDepth = -1;

        std::int32_t Count = Engine->PanelContextCount();
        for (std::int32_t Index = 0; Index < Count; ++Index)
        {
            V8::Address* Slot = Engine->PanelContext(Index);
            if (!Slot || !*Slot)
                continue;

            V8::Local Context = V8::CreateHandle(Isolate, *Slot);
            V8::ContextEnter(Context);

            int Depth = -1;
            V8::Local Root = FindRoot(Isolate, Context, Depth);
            std::string Id = Root ? ReadString(Isolate, Context, Root, "id") : std::string();
            V8::ContextExit(Context);

            if (!ReportedRoots && Root)
                Log("Panel context %d root %s depth %d", Index, Id.c_str(), Depth);

            if (Id == RootPanelId && (BestDepth < 0 || Depth < BestDepth))
            {
                BestContext = Context;
                BestRoot = Root;
                BestDepth = Depth;
            }
        }
        ReportedRoots = true;

        if (!BestContext)
            return false;

        RootContexts[SlotIndex] = V8::Globalize(Isolate, BestContext);
        RootPanels[SlotIndex] = V8::Globalize(Isolate, BestRoot);
        Log("Panorama root %s depth %d", RootPanelId, BestDepth);
        return RootContexts[SlotIndex] && RootPanels[SlotIndex];
    }
}

Panorama::Scope::Scope(RootKind Kind)
{
    UIEngine* Engine = UIEngine::Get();
    if (!Engine)
        return;

    Isolate = Engine->Isolate();
    if (!Isolate)
        return;

    V8::IsolateEnter(Isolate);
    V8::OpenHandleScope(Handles, Isolate);
    V8::OpenTryCatch(Catch, Isolate);

    const int SlotIndex = static_cast<int>(Kind);
    if (!RootContexts[SlotIndex] && !SelectRootContext(Isolate, Engine, Kind))
        return;

    Context = V8::CreateHandle(Isolate, *RootContexts[SlotIndex]);
    V8::ContextEnter(Context);

    PreviousIsolate = CurrentIsolate;
    PreviousContext = CurrentContext;
    PreviousRoot = CurrentRoot;
    PreviousKind = CurrentKind;
    CurrentRoot = RootPanels[SlotIndex];
    CurrentKind = Kind;
    CurrentIsolate = Isolate;
    CurrentContext = Context;
}

Panorama::Scope::~Scope()
{
    if (!Isolate)
        return;

    if (V8::HasCaught(Catch) && ReportedExceptions < 20)
    {
        ++ReportedExceptions;
        V8::Local Error = V8::Exception(Catch);
        LogE("JS exception: %s", Error ? V8::ToString(Isolate, Error).c_str() : "?");
    }

    if (Context)
    {
        CurrentIsolate = PreviousIsolate;
        CurrentContext = PreviousContext;
        CurrentRoot = PreviousRoot;
        CurrentKind = PreviousKind;
        V8::ContextExit(Context);
    }

    V8::CloseTryCatch(Catch);
    V8::CloseHandleScope(Handles);
    V8::IsolateExit(Isolate);
}

bool Panorama::Scope::IsValid() const
{
    return Context != nullptr;
}

void* Panorama::Isolate()
{
    return CurrentIsolate;
}

V8::Local Panorama::Context()
{
    return CurrentContext;
}

V8::Local Panorama::Global()
{
    return V8::ContextGlobal(CurrentContext);
}

V8::Local Panorama::Dollar()
{
    return V8::Get(CurrentIsolate, Global(), CurrentContext, "$");
}

V8::Local Panorama::String(const char* Text)
{
    return V8::NewString(CurrentIsolate, Text);
}

V8::Local Panorama::Number(double Value)
{
    return V8::NewNumber(CurrentIsolate, Value);
}

V8::Local Panorama::Function(Handler Callback, std::intptr_t Argument)
{
    Bindings.push_back(std::make_unique<Binding>(Binding { Callback, Argument, CurrentKind }));
    return V8::NewFunction(CurrentIsolate, CurrentContext, Trampoline, Bindings.back().get());
}

V8::Local Panorama::Invoke(V8::Local Object, const char* Method, int Argc, V8::Local* Argv)
{
    if (!Object || !V8::IsObject(Object))
        return nullptr;

    V8::Local Callee = V8::Get(CurrentIsolate, Object, CurrentContext, Method);
    if (!Callee || !V8::IsFunction(Callee))
        return nullptr;

    return V8::Call(Callee, CurrentContext, Object, Argc, Argv);
}

V8::Local Panorama::Root()
{
    if (!CurrentRoot || !*CurrentRoot || !CurrentIsolate)
        return nullptr;
    return V8::CreateHandle(CurrentIsolate, *CurrentRoot);
}

void Panorama::InvalidateRoot(RootKind Kind)
{
    const int Index = static_cast<int>(Kind);
    if (RootPanels[Index]) V8::DisposeGlobal(RootPanels[Index]);
    if (RootContexts[Index]) V8::DisposeGlobal(RootContexts[Index]);
    RootPanels[Index] = nullptr;
    RootContexts[Index] = nullptr;
}
