// This whole plugin intentionally implemented in 1 file (for a truly minimalistic v1 of BIND) - Under 220 LOC

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>
#include <spdlog/sinks/basic_file_sink.h>

#define _Log_(...) SKSE::log::info(__VA_ARGS__)

namespace SkyrimScripting::Bind {

    RE::TESForm* DefaultBaseFormForCreatingObjects;
    RE::TESObjectREFR* LocationForPlacingObjects;
    std::string FilePath;
    unsigned int LineNumber;
    std::string ScriptName;
    RE::BSScript::IVirtualMachine* vm;
    constexpr auto BINDING_FILES_FOLDER_ROOT = "Data/Scripts/Bindings";

    void LowerCase(std::string& text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }
    RE::TESForm* LookupFormID(RE::FormID formID) {
        auto* form = RE::TESForm::LookupByID(formID);
        if (form)
            return form;
        else
            _Log_("[BIND] Error [{}:{}] ({}) Form ID '{:x}' does not exist", FilePath, LineNumber, ScriptName, formID);
        return {};
    }
    RE::TESForm* LookupEditorID(const std::string& editorID) {
        auto* form = RE::TESForm::LookupByEditorID(editorID);
        if (form)
            return form;
        else
            _Log_("[BIND] Error [{}:{}] ({}) Form Editor ID '{}' does not exist (Are you using po3 Tweaks?)", FilePath, LineNumber, ScriptName, editorID);
        return {};
    }
    bool FormHasScriptAttached(RE::VMHandle handle, const std::string scriptName) {
        RE::BSScript::Internal::VirtualMachine* _vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        RE::BSSpinLockGuard lock{_vm->attachedScriptsLock};
        auto found = _vm->attachedScripts.find(handle);
        if (found != _vm->attachedScripts.end()) {
            RE::BSFixedString bsScriptName{scriptName};
            auto& scripts = found->second;
            for (auto& script : scripts) {
                if (script->type->name == bsScriptName) return true;
            }
        }
        return false;
    }
    void Bind_Form(RE::TESForm* form) {
        auto handle = vm->GetObjectHandlePolicy()->GetHandleForObject(form->GetFormType(), form);
        if (FormHasScriptAttached(handle, ScriptName)) {
            _Log_("[BIND] Script {} already attached to form {} ({})", ScriptName, form->GetFormID(), form->GetName());
        } else {
            RE::BSTSmartPointer<RE::BSScript::Object> object;
            vm->CreateObject(ScriptName, object);
            vm->GetObjectBindPolicy()->BindObject(object, handle);
            _Log_("[BIND] Bound script {} to form {} ({})", ScriptName, form->GetFormID(), form->GetName());
        }
    }
    void Bind_GeneratedObject(RE::TESForm* baseForm = nullptr) {
        if (!baseForm) baseForm = DefaultBaseFormForCreatingObjects;  // By default, simply puts a fork next to the WEMerchantChest in the WEMerchantChests cell. Forking awesome.
        auto niPointer = LocationForPlacingObjects->PlaceObjectAtMe(skyrim_cast<RE::TESBoundObject*, RE::TESForm>(baseForm), false);
        if (niPointer)
            Bind_Form(niPointer.get());
        else
            _Log_("[BIND] Error [{}:{}] ({}) Could not generate object ({}, {})", FilePath, LineNumber, ScriptName, baseForm->GetFormID(), baseForm->GetName());
    }
    void Bind_GeneratedObject_BaseEditorID(const std::string& baseEditorId) {
        auto* form = LookupEditorID(baseEditorId);
        if (form) Bind_GeneratedObject(form);
    }
    void Bind_GeneratedObject_BaseFormID(RE::FormID baseFormID) {
        auto* form = LookupFormID(baseFormID);
        if (form) Bind_GeneratedObject(form);
    }
    void Bind_GeneratedQuest(std::string editorID = "") {
        auto* form = RE::IFormFactory::GetConcreteFormFactoryByType<RE::TESQuest>()->Create();
        if (!editorID.empty()) form->SetFormEditorID(editorID.c_str());
        Bind_Form(form);
    }
    void Bind_FormID(RE::FormID formID) {
        auto* form = LookupFormID(formID);
        if (form) Bind_Form(form);
    }
    void Bind_EditorID(const std::string& editorID) {
        auto* form = LookupEditorID(editorID);
        if (form) Bind_Form(form);
    }
    bool IsUnderstoodScriptParentType(std::string parentTypeName) {
        LowerCase(parentTypeName);
        if (parentTypeName == "quest" || parentTypeName == "actor" || parentTypeName == "objectreference") return true;
        return false;
    }
    void AutoBindBasedOnScriptExtends() {
        RE::BSTSmartPointer<RE::BSScript::ObjectTypeInfo> typeInfo;
        vm->GetScriptObjectType(ScriptName, typeInfo);
        std::string parentName;
        auto parent = typeInfo->parentTypeInfo;
        while (parent && !IsUnderstoodScriptParentType(parentName)) {
            parentName = parent->GetName();
            parent = parent->parentTypeInfo;
        }
        if (parentName.empty()) {
            _Log_("[BIND] Error [{}:{}] ({}) Cannot auto-bind to a script which does not `extends` anything", FilePath, LineNumber, ScriptName);
            return;
        }
        LowerCase(parentName);
        if (parentName == "quest")
            Bind_GeneratedQuest();
        else if (parentName == "actor")
            Bind_FormID(0x14);
        else if (parentName == "objectreference")
            Bind_GeneratedObject();
        else
            _Log_("[BIND] Error [{}:{}] ({}) No default BIND behavior available for script which `extends` {}", FilePath, LineNumber, ScriptName, parentName);
    }
    void ProcessBindingLine(std::string line) {
        if (line.empty()) return;
        std::replace(line.begin(), line.end(), '\t', ' ');
        std::istringstream lineStream{line};
        lineStream >> ScriptName;
        if (ScriptName.empty() || ScriptName.starts_with('#') || ScriptName.starts_with("//")) return;
        if (!vm->TypeIsValid(ScriptName)) {
            _Log_("[BIND] Error [{}:{}] Script '{}' does not exist", FilePath, LineNumber, ScriptName);
            return;
        }
        std::string bindTarget;
        lineStream >> bindTarget;
        LowerCase(bindTarget);
        if (bindTarget.empty()) {
            AutoBindBasedOnScriptExtends();
            return;
        } else if (bindTarget.starts_with("0x"))
            Bind_FormID(std::stoi(bindTarget, 0, 16));
        else if (bindTarget == "$player")
            Bind_FormID(0x14);
        else if (bindTarget.starts_with("$quest("))
            Bind_GeneratedQuest(bindTarget.substr(7, bindTarget.size() - 8));
        else if (bindTarget == "$quest")
            Bind_GeneratedQuest();
        else if (bindTarget == "$object")
            Bind_GeneratedObject();
        else if (bindTarget.starts_with("$object(0x"))
            Bind_GeneratedObject_BaseFormID(std::stoi(bindTarget.substr(8, bindTarget.size() - 9), 0, 16));
        else if (bindTarget.starts_with("$object("))
            Bind_GeneratedObject_BaseEditorID(bindTarget.substr(8, bindTarget.size() - 9));
        else
            Bind_EditorID(bindTarget);
    }
    void ProcessBindingFile() {
        _Log_("[BIND] Reading Binding File: {}", FilePath);
        LineNumber = 1;
        std::string line;
        std::ifstream file{FilePath, std::ios::in};
        while (std::getline(file, line)) {
            LineNumber++;
            try {
                ProcessBindingLine(line);
            } catch (...) {
                _Log_("[BIND] Error [{}:{}]", FilePath, LineNumber);
            }
        }
        file.close();
    }
    void ProcessAllBindingFiles() {
        if (!std::filesystem::is_directory(BINDING_FILES_FOLDER_ROOT)) {
            _Log_("[BIND] {} folder not found", BINDING_FILES_FOLDER_ROOT);
            return;
        }
        _Log_("[BIND] Processing Bind Scripts");
        for (auto& file : std::filesystem::directory_iterator(BINDING_FILES_FOLDER_ROOT)) {
            FilePath = file.path().string();
            ProcessBindingFile();
        }
    }
    void Start() {
        vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        DefaultBaseFormForCreatingObjects = RE::TESForm::LookupByID(0xAEBF3);             // DwarvenFork
        LocationForPlacingObjects = RE::TESForm::LookupByID<RE::TESObjectREFR>(0xBBCD1);  // The chest in WEMerchantChests
        ProcessAllBindingFiles();
    }
    void SetupLog() {
        auto logsFolder = SKSE::log::log_directory();
        auto pluginName = SKSE::PluginDeclaration::GetSingleton()->GetName();
        auto logFilePath = *logsFolder / std::format("{}.log", pluginName);
        auto fileLoggerPtr = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath.string(), true);
        auto loggerPtr = std::make_shared<spdlog::logger>("log", std::move(fileLoggerPtr));
        spdlog::set_default_logger(std::move(loggerPtr));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::info);
        spdlog::set_pattern("%v");
    }
    SKSEPluginLoad(const SKSE::LoadInterface* skse) {
        SKSE::Init(skse);
        SetupLog();
        SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* message) {
            switch (message->type) {
                case SKSE::MessagingInterface::kNewGame:
                    _Log_("[BIND] New game started");
                case SKSE::MessagingInterface::kPostLoadGame:
                    _Log_("[BIND] Game loaded");
                    Start();
                    break;
                default:
                    break;
            }
        });
        return true;
    }
}
