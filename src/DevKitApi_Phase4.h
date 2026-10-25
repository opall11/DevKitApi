// DevKitApi — phase 4 ECRITURE de graphs Blueprint (2026-09-04). Inclus par DevKitApi.cpp (une seule unite de
// compilation), apres les helpers JSON/reflexion et avant ExecuteCommandBody.
//
// Commandes : add_variable, remove_variable, add_function, remove_graph, paste_t3d, compile_blueprint (avec messages).
// Toutes les ecritures exigent que le package du Blueprint soit sous la liste blanche (IsWritablePackagePath).
//
// Preuves (refs\exports_utiles.md §8 + refs\layouts.md) :
//  - tous les symboles ci-dessous sont [EXPORT] (exports\UnrealEd.txt, BlueprintGraph.txt, Engine.txt, CoreUObject.txt, Core.txt)
//  - index de vtable UEdGraphNode (lus dans la vtable exportee ??_7UEdGraphNode@@6B@ d'Engine.dll et re-verifies dans
//    ??_7UK2Node_CallFunction@@6B@ de BlueprintGraph.dll, meme position) :
//        AllocateDefaultPins = 84, ReconstructNode = 86, CanPasteHere(graph,schema) = 88, PostPasteNode = 92, PostPlacedNewNode = 104
//  - FEdGraphPinType construit UNIQUEMENT par reflexion (UScriptStruct /Script/Engine.EdGraphPin:EdGraphPinType :
//    PinCategory, PinSubCategory, PinSubCategoryObject (weak), PinSubCategoryMemberReference, bIsArray, bIsReference,
//    bIsWeakPointer, bIsConst — ce build Wildcard a bIsConst) : InitializeScriptStruct + ImportText par champ.
//  - TSet<UEdGraphNode*> (sortie d'ImportNodesFromText) : on ne fait que LIRE Data(+0)/Num(+8) (TSparseArray -> TArray en tete,
//    element TSetElement = {ptr, HashNextId, HashIndex} = 16 octets) puis on libere Data via FMemory::Free ; les buffers
//    secondaires (bitarray +32, hash +64) ne sont liberes que si NumBits(+40) == Num (auto-verification du layout).

typedef void*     (*FMemoryRealloc_t)(void* ptr, size_t count, unsigned alignment);
typedef void      (*FStringCtorN_t)(FStringRaw* self, int count, const wchar_t* src);
typedef void      (*FStringDtor_t)(FStringRaw* self);
typedef void      (*RedirDevice_t)(void* redirector, void* device);
typedef int       (*StructSize_t)(const void* ustruct);
typedef void      (*StructInitDestroy_t)(const void* sstruct, void* data, int arrayDim);
typedef bool      (*AddMemberVariable_t)(void* bp, const FNameRaw* name, const void* pinType, const FStringRaw* defaultValue);
typedef void      (*BpName_t)(void* bp, const FNameRaw* name);
typedef bool      (*BpNameBool_t)(void* bp, const FNameRaw* name);
typedef int       (*BpNameInt_t)(const void* bp, const FNameRaw* name);
typedef uint64_t* (*BpNameFlags_t)(void* bp, const FNameRaw* name);
typedef void      (*BpNameName_t)(void* bp, const FNameRaw* a, const FNameRaw* b);
typedef void      (*SetVarCategory_t)(void* bp, const FNameRaw* var, const void* localScope, const FNameRaw* cat, bool bDontRecompile);
typedef void*     (*CreateNewGraph_t)(void* outer, const FNameRaw* name, void* graphClass, void* schemaClass, bool bTransient);
typedef void      (*RemoveGraph_t)(void* bp, void* graph, int flags);
typedef void      (*SchemaGraphObj_t)(const void* schema, void* graph, void* obj);      // CreateFunctionGraphTerminators(UEdGraph&, UClass*|UFunction*)
typedef void      (*SchemaGraphInt_t)(const void* schema, const void* graph, int v);    // AddExtraFunctionFlags
typedef void      (*SchemaGraphBool_t)(const void* schema, const void* graph, bool v);  // MarkFunctionEntryAsEditable
typedef void*     (*CreateUserDefinedPin_t)(void* node, const FStringRaw* name, const void* pinType);
typedef void*     (*FindFunctionByName_t)(const void* cls, const FNameRaw* name, int includeSuper);
typedef void      (*ImportNodesFromText_t)(void* graph, const FStringRaw* text, void* nodeSet);
typedef void      (*GraphAddNode_t)(void* graph, void* node, bool bFromUI, bool bSelectNewNode);
typedef void*     (*StaticConstructObject_t)(void* cls, void* outer, const FNameRaw* name, int flags, void* templ, bool bCopyTransients, void* instGraph);
typedef void*     (*ClassOfW_t)(const wchar_t* pkg);   // GetPrivateStaticClass(const TCHAR*)
typedef void*     (*NodeFindPin_t)(const void* node, const FStringRaw* pinName);          // UEdGraphNode::FindPin
typedef bool      (*SchemaTryConnect_t)(const void* schema, void* a, void* b);            // UEdGraphSchema_K2::TryCreateConnection (virtuelle, classe exacte = K2 -> appel direct)
typedef void      (*PinPin_t)(void* pin, void* other);                                    // UEdGraphPin::MakeLinkTo / BreakLinkTo
typedef void      (*PinVoid_t)(void* pin);                                                // UEdGraphPin::BreakAllPinLinks

struct EngineApi4 {
    FMemoryRealloc_t Realloc = nullptr;
    FStringCtorN_t FStringCtorN = nullptr; FStringDtor_t FStringDtor = nullptr;
    GetRedirector_t GetRedirector = nullptr; RedirDevice_t AddOutputDevice = nullptr, RemoveOutputDevice = nullptr;
    StructSize_t GetPropertiesSize = nullptr; StructInitDestroy_t InitializeScriptStruct = nullptr, DestroyScriptStruct = nullptr;
    AddMemberVariable_t AddMemberVariable = nullptr; BpName_t RemoveMemberVariable = nullptr, ValidateBlueprintChildVariables = nullptr;
    BpNameInt_t FindNewVariableIndex = nullptr; BpNameFlags_t GetVarPropertyFlags = nullptr; BpNameName_t SetVarRepNotifyFunc = nullptr;
    uint8_t* (*GetVarRepCondition)(void* bp, const FNameRaw* name) = nullptr;
    SetVarCategory_t SetVarCategory = nullptr; BpNameBool_t IsGraphNameUnique = nullptr;
    CreateNewGraph_t CreateNewGraph = nullptr; RemoveGraph_t RemoveGraph = nullptr;
    SchemaGraphObj_t CreateTerminatorsClass = nullptr, CreateTerminatorsFunc = nullptr;
    SchemaGraphInt_t AddExtraFunctionFlags = nullptr; SchemaGraphBool_t MarkFunctionEntryAsEditable = nullptr;
    CreateUserDefinedPin_t CreateUserDefinedPin = nullptr; FindFunctionByName_t FindFunctionByName = nullptr;
    ImportNodesFromText_t ImportNodesFromText = nullptr; GraphAddNode_t GraphAddNode = nullptr;
    ObjVoid_t CreateNewGuid = nullptr, NotifyGraphChanged = nullptr, MarkBlueprintAsStructurallyModified = nullptr;
    StaticConstructObject_t StaticConstructObject = nullptr;
    StaticClassFn_t UEdGraphClass = nullptr, K2SchemaClass = nullptr;
    ClassOfW_t K2FunctionEntryClass = nullptr, K2FunctionResultClass = nullptr, K2CustomEventClass = nullptr, K2NodeClass = nullptr;
    NodeFindPin_t NodeFindPin = nullptr; SchemaTryConnect_t TryCreateConnection = nullptr;
    PinPin_t PinMakeLinkTo = nullptr, PinBreakLinkTo = nullptr; PinVoid_t PinBreakAllLinks = nullptr;
    void (*TrySetDefaultValue)(const void* schema, void* pin, const FStringRaw* value) = nullptr;   // virtuelles K2, classe exacte -> appel direct
    void (*TrySetDefaultObject)(const void* schema, void* pin, void* obj) = nullptr;
    // PIE : UEditorEngine::RequestPlaySession(bool bAtPlayerStart, TSharedPtr<ILevelViewport>, bool bSimulate, const FVector*, const FRotator*, int32 DestConsole=-1, bool bMobile)
    void (*RequestPlaySession)(void* editor, bool atPlayerStart, TSharedPtrRaw* viewport, bool simulate, const void* loc, const void* rot, int destConsole, bool mobile) = nullptr;
    void (*RequestEndPlayMap)(void* editor) = nullptr;
    void* (*GetFirstPlayerController)(const void* world) = nullptr;
    // FString APlayerController::ConsoleCommand(const FString&, bool bWriteToLog) : virtuelle, mais AUCUNE surcharge native dans
    // ShooterGame (exports) et un BP ne surcharge pas une virtuelle C++ -> l'export de base est l'unique implementation, appel direct.
    FStringRaw* (*PCConsoleCommand)(void* pc, FStringRaw* out, const FStringRaw* cmd, bool bWriteToLog) = nullptr;
    void* PinTypeStruct = nullptr;   // UScriptStruct FEdGraphPinType (trouve par chemin)
    bool ok = false; std::string missing;
    void Init() {
        if (ok || !missing.empty()) return;
        struct R { const wchar_t* dll; const char* sym; void** dst; };
        const R tab[] = {
            { L"UE4Editor-Core.dll",          "?Realloc@FMemory@@SAPEAXPEAX_KI@Z", (void**)&Realloc },
            { L"UE4Editor-Core.dll",          "??0FString@@QEAA@HPEB_W@Z", (void**)&FStringCtorN },
            { L"UE4Editor-Core.dll",          "??1FString@@QEAA@XZ", (void**)&FStringDtor },
            { L"UE4Editor-Core.dll",          "?Get@FOutputDeviceRedirector@@SAPEAV1@XZ", (void**)&GetRedirector },
            { L"UE4Editor-Core.dll",          "?AddOutputDevice@FOutputDeviceRedirector@@QEAAXPEAVFOutputDevice@@@Z", (void**)&AddOutputDevice },
            { L"UE4Editor-Core.dll",          "?RemoveOutputDevice@FOutputDeviceRedirector@@QEAAXPEAVFOutputDevice@@@Z", (void**)&RemoveOutputDevice },
            { L"UE4Editor-CoreUObject.dll",   "?GetPropertiesSize@UStruct@@QEBAHXZ", (void**)&GetPropertiesSize },
            { L"UE4Editor-CoreUObject.dll",   "?InitializeScriptStruct@UScriptStruct@@QEBAXPEAXH@Z", (void**)&InitializeScriptStruct },
            { L"UE4Editor-CoreUObject.dll",   "?DestroyScriptStruct@UScriptStruct@@QEBAXPEAXH@Z", (void**)&DestroyScriptStruct },
            { L"UE4Editor-CoreUObject.dll",   "?FindFunctionByName@UClass@@QEBAPEAVUFunction@@VFName@@W4Type@EIncludeSuperFlag@@@Z", (void**)&FindFunctionByName },
            { L"UE4Editor-CoreUObject.dll",   "?StaticConstructObject@@YAPEAVUObject@@PEAVUClass@@PEAV1@VFName@@W4EObjectFlags@@1_NPEAUFObjectInstancingGraph@@@Z", (void**)&StaticConstructObject },
            { L"UE4Editor-UnrealEd.dll",      "?AddMemberVariable@FBlueprintEditorUtils@@SA_NPEAVUBlueprint@@AEBVFName@@AEBUFEdGraphPinType@@AEBVFString@@@Z", (void**)&AddMemberVariable },
            { L"UE4Editor-UnrealEd.dll",      "?RemoveMemberVariable@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@AEBVFName@@@Z", (void**)&RemoveMemberVariable },
            { L"UE4Editor-UnrealEd.dll",      "?ValidateBlueprintChildVariables@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@AEBVFName@@@Z", (void**)&ValidateBlueprintChildVariables },
            { L"UE4Editor-UnrealEd.dll",      "?FindNewVariableIndex@FBlueprintEditorUtils@@SAHPEBVUBlueprint@@AEBVFName@@@Z", (void**)&FindNewVariableIndex },
            { L"UE4Editor-UnrealEd.dll",      "?GetBlueprintVariablePropertyFlags@FBlueprintEditorUtils@@SAPEA_KPEAVUBlueprint@@AEBVFName@@@Z", (void**)&GetVarPropertyFlags },
            { L"UE4Editor-UnrealEd.dll",      "?SetBlueprintVariableRepNotifyFunc@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@AEBVFName@@1@Z", (void**)&SetVarRepNotifyFunc },
            { L"UE4Editor-UnrealEd.dll",      "?GetBlueprintVariableRepCondition@FBlueprintEditorUtils@@SAPEAEPEAVUBlueprint@@AEBVFName@@@Z", (void**)&GetVarRepCondition },
            { L"UE4Editor-UnrealEd.dll",      "?SetBlueprintVariableCategory@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@AEBVFName@@PEBVUStruct@@1_N@Z", (void**)&SetVarCategory },
            { L"UE4Editor-UnrealEd.dll",      "?IsGraphNameUnique@FBlueprintEditorUtils@@SA_NPEAVUBlueprint@@AEBVFName@@@Z", (void**)&IsGraphNameUnique },
            { L"UE4Editor-UnrealEd.dll",      "?CreateNewGraph@FBlueprintEditorUtils@@SAPEAVUEdGraph@@PEAVUObject@@AEBVFName@@V?$TSubclassOf@VUEdGraph@@@@V?$TSubclassOf@VUEdGraphSchema@@@@_N@Z", (void**)&CreateNewGraph },
            { L"UE4Editor-UnrealEd.dll",      "?RemoveGraph@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@PEAVUEdGraph@@W4Type@EGraphRemoveFlags@@@Z", (void**)&RemoveGraph },
            { L"UE4Editor-UnrealEd.dll",      "?ImportNodesFromText@FEdGraphUtilities@@SAXPEAVUEdGraph@@AEBVFString@@AEAV?$TSet@PEAVUEdGraphNode@@U?$DefaultKeyFuncs@PEAVUEdGraphNode@@$0A@@@VFDefaultSetAllocator@@@@@Z", (void**)&ImportNodesFromText },
            { L"UE4Editor-UnrealEd.dll",      "?MarkBlueprintAsStructurallyModified@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@@Z", (void**)&MarkBlueprintAsStructurallyModified },
            { L"UE4Editor-BlueprintGraph.dll","?CreateFunctionGraphTerminators@UEdGraphSchema_K2@@UEBAXAEAVUEdGraph@@PEAVUClass@@@Z", (void**)&CreateTerminatorsClass },
            { L"UE4Editor-BlueprintGraph.dll","?CreateFunctionGraphTerminators@UEdGraphSchema_K2@@UEBAXAEAVUEdGraph@@PEAVUFunction@@@Z", (void**)&CreateTerminatorsFunc },
            { L"UE4Editor-BlueprintGraph.dll","?AddExtraFunctionFlags@UEdGraphSchema_K2@@QEBAXPEBVUEdGraph@@H@Z", (void**)&AddExtraFunctionFlags },
            { L"UE4Editor-BlueprintGraph.dll","?MarkFunctionEntryAsEditable@UEdGraphSchema_K2@@QEBAXPEBVUEdGraph@@_N@Z", (void**)&MarkFunctionEntryAsEditable },
            { L"UE4Editor-BlueprintGraph.dll","?CreateUserDefinedPin@UK2Node_EditablePinBase@@QEAAPEAVUEdGraphPin@@AEBVFString@@AEBUFEdGraphPinType@@@Z", (void**)&CreateUserDefinedPin },
            { L"UE4Editor-BlueprintGraph.dll","?StaticClass@UEdGraphSchema_K2@@SAPEAVUClass@@XZ", (void**)&K2SchemaClass },
            { L"UE4Editor-BlueprintGraph.dll","?GetPrivateStaticClass@UK2Node_FunctionEntry@@CAPEAVUClass@@PEB_W@Z", (void**)&K2FunctionEntryClass },
            { L"UE4Editor-BlueprintGraph.dll","?GetPrivateStaticClass@UK2Node_FunctionResult@@CAPEAVUClass@@PEB_W@Z", (void**)&K2FunctionResultClass },
            { L"UE4Editor-BlueprintGraph.dll","?GetPrivateStaticClass@UK2Node_CustomEvent@@CAPEAVUClass@@PEB_W@Z", (void**)&K2CustomEventClass },
            { L"UE4Editor-BlueprintGraph.dll","?GetPrivateStaticClass@UK2Node@@CAPEAVUClass@@PEB_W@Z", (void**)&K2NodeClass },
            { L"UE4Editor-Engine.dll",        "?StaticClass@UEdGraph@@SAPEAVUClass@@XZ", (void**)&UEdGraphClass },
            { L"UE4Editor-Engine.dll",        "?AddNode@UEdGraph@@QEAAXPEAVUEdGraphNode@@_N1@Z", (void**)&GraphAddNode },
            { L"UE4Editor-Engine.dll",        "?CreateNewGuid@UEdGraphNode@@QEAAXXZ", (void**)&CreateNewGuid },
            { L"UE4Editor-Engine.dll",        "?NotifyGraphChanged@UEdGraph@@QEAAXXZ", (void**)&NotifyGraphChanged },
            { L"UE4Editor-Engine.dll",        "?FindPin@UEdGraphNode@@QEBAPEAVUEdGraphPin@@AEBVFString@@@Z", (void**)&NodeFindPin },
            { L"UE4Editor-Engine.dll",        "?MakeLinkTo@UEdGraphPin@@QEAAXPEAV1@@Z", (void**)&PinMakeLinkTo },
            { L"UE4Editor-Engine.dll",        "?BreakLinkTo@UEdGraphPin@@QEAAXPEAV1@@Z", (void**)&PinBreakLinkTo },
            { L"UE4Editor-Engine.dll",        "?BreakAllPinLinks@UEdGraphPin@@QEAAXXZ", (void**)&PinBreakAllLinks },
            { L"UE4Editor-BlueprintGraph.dll","?TryCreateConnection@UEdGraphSchema_K2@@UEBA_NPEAVUEdGraphPin@@0@Z", (void**)&TryCreateConnection },
            { L"UE4Editor-BlueprintGraph.dll","?TrySetDefaultValue@UEdGraphSchema_K2@@UEBAXAEAVUEdGraphPin@@AEBVFString@@@Z", (void**)&TrySetDefaultValue },
            { L"UE4Editor-BlueprintGraph.dll","?TrySetDefaultObject@UEdGraphSchema_K2@@UEBAXAEAVUEdGraphPin@@PEAVUObject@@@Z", (void**)&TrySetDefaultObject },
            { L"UE4Editor-UnrealEd.dll",      "?RequestPlaySession@UEditorEngine@@QEAAX_NV?$TSharedPtr@VILevelViewport@@$0A@@@0PEBVFVector@@PEBVFRotator@@H0@Z", (void**)&RequestPlaySession },
            { L"UE4Editor-UnrealEd.dll",      "?RequestEndPlayMap@UEditorEngine@@QEAAXXZ", (void**)&RequestEndPlayMap },
            { L"UE4Editor-Engine.dll",        "?GetFirstPlayerController@UWorld@@QEBAPEAVAPlayerController@@XZ", (void**)&GetFirstPlayerController },
            { L"UE4Editor-Engine.dll",        "?ConsoleCommand@APlayerController@@UEAA?AVFString@@AEBV2@_N@Z", (void**)&PCConsoleCommand },
        };
        for (const R& r : tab) { *r.dst = (void*)Resolve(r.dll, r.sym); if (!*r.dst) missing += std::string(r.sym) + " "; }
        ok = missing.empty();
        if (!ok) FileLog("EngineApi4: exports manquants : %s", missing.c_str());
    }
};
static EngineApi4& Api4() { static EngineApi4 a; return a; }

enum { VT_AllocateDefaultPins = 84, VT_ReconstructNode = 86, VT_PostPasteNode = 92, VT_PostPlacedNewNode = 104 };
static void CallVirtVoid(void* obj, int slot) { typedef void (*Fn)(void*); ((Fn)((*(void***)obj)[slot]))(obj); }

struct TArrayRaw { void* Data; int32_t Num; int32_t Max; };   // layouts.md (16 octets)

static bool TArrayPtrAdd(TArrayRaw* arr, void* elem)
{
    if (arr->Num >= arr->Max) {
        int32_t newMax = arr->Max > 0 ? arr->Max * 2 : 4;
        void* nd = Api4().Realloc(arr->Data, (size_t)newMax * sizeof(void*), 0);
        if (!nd) return false;
        arr->Data = nd; arr->Max = newMax;
    }
    ((void**)arr->Data)[arr->Num++] = elem;
    return true;
}

static bool P4Ready(Command* c)
{
    Api().Init(); Api3().Init(); Api4().Init();
    if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return false; }
    if (!Api4().ok) { c->reply = ErrReply(c, "phase4 exports not resolved: " + Api4().missing); return false; }
    if (!EditorReady()) { c->reply = ErrReply(c, "editor not ready"); return false; }
    return true;
}

// Blueprint cible + liste blanche sur son package reel
static void* P4Blueprint(Command* c, const char* key = "blueprint")
{
    const JsonValue* bv = ArgStr(c->args, key);
    if (!bv) { c->reply = ErrReply(c, std::string("missing ") + key); return nullptr; }
    void* bp = ResolveObject(bv->str, true);
    if (!bp) { c->reply = ErrReply(c, "blueprint not found: " + bv->str); return nullptr; }
    if (!Api3().IsA(bp, Api3().UBlueprintClass())) { c->reply = ErrReply(c, "not a UBlueprint: " + PathNameOf(bp)); return nullptr; }
    std::string pkg = PathNameOf(Api3().GetOutermost(bp)), why;
    if (!IsWritablePackagePath(pkg, why)) { c->reply = ErrReply(c, "refused (whitelist): " + pkg + " : " + why); return nullptr; }
    return bp;
}

// TArray<UObject*> UPROPERTY d'un objet (FunctionGraphs, UbergraphPages, Nodes...)
static TArrayRaw* ObjArrayProp(void* obj, const char* name)
{
    PropInfo p; if (!FindProp(obj, name, p) || p.size != 16) return nullptr;
    return (TArrayRaw*)((uint8_t*)obj + p.offset);
}

static bool NameEqualsCI(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    return true;
}

// Graph d'un Blueprint par nom (UbergraphPages / FunctionGraphs / MacroGraphs / DelegateSignatureGraphs) ou par chemin objet complet
static void* FindGraph(void* bp, const std::string& nameOrPath, std::string& kind)
{
    if (nameOrPath.find('.') != std::string::npos || nameOrPath.find(':') != std::string::npos) {
        void* g = ResolveObject(nameOrPath, false);
        if (g && Api3().IsA(g, Api4().UEdGraphClass())) { kind = "path"; return g; }
        return nullptr;
    }
    const char* arrays[] = { "UbergraphPages", "FunctionGraphs", "MacroGraphs", "DelegateSignatureGraphs" };
    for (const char* an : arrays) {
        TArrayRaw* arr = ObjArrayProp(bp, an);
        if (!arr) continue;
        for (int i = 0; i < arr->Num; ++i) {
            void* g = ((void**)arr->Data)[i];
            if (g && NameEqualsCI(NameOf(g), nameOrPath)) { kind = an; return g; }
        }
    }
    return nullptr;
}

// FEdGraphPinType construit par reflexion. Champs JSON : category (bool/byte/int/float/double/string/name/text/object/class/struct/
// exec/wildcard/interface), sub_category (ex. "self"), sub_object (chemin de classe/struct, charge si besoin), is_array, is_reference, is_const.
struct PinTypeBuf {
    uint8_t* data = nullptr; int size = 0; void* sstruct = nullptr;
    ~PinTypeBuf() { if (data) { if (sstruct) Api4().DestroyScriptStruct(sstruct, data, 1); delete[] data; } }
};
static bool BuildPinType(const JsonObject& o, const char* prefix, void* owner, PinTypeBuf& out, std::string& err)
{
    EngineApi4& a = Api4();
    if (!a.PinTypeStruct) a.PinTypeStruct = Api().StaticFindObject(nullptr, ANY_PACKAGE, L"/Script/Engine.EdGraphPin:EdGraphPinType", false);
    if (!a.PinTypeStruct) { err = "UScriptStruct EdGraphPinType not found"; return false; }
    out.sstruct = a.PinTypeStruct;
    out.size = a.GetPropertiesSize(out.sstruct);
    if (out.size <= 0 || out.size > 4096) { err = "bad EdGraphPinType size"; return false; }
    out.data = new uint8_t[out.size]; memset(out.data, 0, out.size);
    a.InitializeScriptStruct(out.sstruct, out.data, 1);
    std::string pre = prefix ? prefix : "";
    auto get = [&](const char* k) -> const JsonValue* { return JsonGet(o, (pre + k).c_str()); };
    const JsonValue* cat = get("category");
    if (!cat || cat->str.empty()) { err = "missing " + pre + "category"; return false; }
    struct F { const char* prop; std::string text; };
    std::vector<F> fields;
    fields.push_back({ "PinCategory", cat->str });
    if (const JsonValue* v = get("sub_category")) if (!v->str.empty()) fields.push_back({ "PinSubCategory", v->str });
    if (const JsonValue* v = get("sub_object")) if (!v->str.empty()) {
        void* so = ResolveObject(v->str, true);
        if (!so) { err = "sub_object not found: " + v->str; return false; }
        fields.push_back({ "PinSubCategoryObject", PathNameOf(so) });
    }
    auto boolArg = [&](const char* k) { const JsonValue* v = get(k); return v && v->isBool && v->b; };
    if (boolArg("is_array")) fields.push_back({ "bIsArray", "True" });
    if (boolArg("is_reference")) fields.push_back({ "bIsReference", "True" });
    if (boolArg("is_const")) fields.push_back({ "bIsConst", "True" });
    if (boolArg("is_weak")) fields.push_back({ "bIsWeakPointer", "True" });
    std::string errText; FCaptureDevice errDev{ g_captureVtable, false, true, &errText, 0, false };
    for (const F& f : fields) {
        FNameRaw fn; MakeFName(Utf8ToWide(f.prop).c_str(), fn);
        void* prop = Api3().FindPropertyByName(out.sstruct, &fn);
        if (!prop) { err = std::string("EdGraphPinType has no property ") + f.prop; return false; }
        int off = Api3().GetOffset(prop);
        std::wstring w = Utf8ToWide(f.text);
        if (!Api3().ImportText(prop, w.c_str(), out.data + off, 0, owner, &errDev)) { err = std::string("ImportText failed for ") + f.prop + " = " + f.text + " : " + errText; return false; }
    }
    return true;
}

// FString temporaire (buffer FMemory) pour passer une const FString& au moteur
struct FStringTmp {
    FStringRaw s{};
    explicit FStringTmp(const std::string& utf8) { std::wstring w = Utf8ToWide(utf8); Api4().FStringCtorN(&s, (int)w.size(), w.c_str()); }
    ~FStringTmp() { Api4().FStringDtor(&s); }
};

static std::string JsonBool(bool b) { return b ? "true" : "false"; }

// Premier noeud du graph d'une classe donnee (ex. K2Node_FunctionEntry / K2Node_FunctionResult)
static void* FindNodeOfClass(void* graph, void* cls)
{
    TArrayRaw* nodes = ObjArrayProp(graph, "Nodes");
    if (!nodes) return nullptr;
    for (int i = 0; i < nodes->Num; ++i) { void* n = ((void**)nodes->Data)[i]; if (n && Api3().IsA(n, cls)) return n; }
    return nullptr;
}

static void SetIntProp(void* obj, const char* name, int v) { PropInfo p; if (FindProp(obj, name, p) && p.size == 4) *(int32_t*)((uint8_t*)obj + p.offset) = v; }
static int  GetIntProp(void* obj, const char* name, int def) { PropInfo p; return (FindProp(obj, name, p) && p.size == 4) ? *(int32_t*)((uint8_t*)obj + p.offset) : def; }
static std::string GetStrProp(void* obj, const char* name) { PropInfo p; if (!FindProp(obj, name, p) || p.size != 16) return ""; FStringRaw* s = (FStringRaw*)((uint8_t*)obj + p.offset); return (s->Data && s->Num > 0) ? WideToUtf8(s->Data, s->Num - 1) : ""; }
static std::string ExportProp(void* obj, const char* name) { PropInfo p; if (!FindProp(obj, name, p)) return ""; FStringRaw out{}; Api3().ExportText_InContainer(p.prop, 0, &out, obj, nullptr, obj, 0, nullptr); return TakeFString(out); }

// Noeud d'un graph par nom d'objet (ex. "K2Node_CallFunction_12") ; pin par PinName ("then", "ReturnValue")
static void* FindNodeByName(void* graph, const std::string& name) { void* n = Api().StaticFindObject(nullptr, graph, Utf8ToWide(name).c_str(), false); return n; }
static void* FindPinByName(void* node, const std::string& pinName) { FStringTmp s(pinName); return Api4().NodeFindPin(node, &s.s); }

// JSON d'un noeud : nom, classe, pins (nom, direction, type, nb de liens)
static std::string NodeJson(void* n)
{
    std::string pinsJ;
    if (TArrayRaw* pins = ObjArrayProp(n, "Pins")) {
        for (int i = 0; i < pins->Num; ++i) {
            void* pin = ((void**)pins->Data)[i]; if (!pin) continue;
            PropInfo dp; int dir = (FindProp(pin, "Direction", dp) && dp.size == 1) ? *((uint8_t*)pin + dp.offset) : -1;
            TArrayRaw* lt = ObjArrayProp(pin, "LinkedTo");
            pinsJ += (pinsJ.empty() ? "" : ",") + std::string("{\"name\":") + JsonStr(GetStrProp(pin, "PinName")) + ",\"dir\":" + JsonStr(dir == 1 ? "out" : "in")
                   + ",\"type\":" + JsonStr(ExportProp(pin, "PinType")) + ",\"links\":" + std::to_string(lt ? lt->Num : 0)
                   + ",\"default\":" + JsonStr(GetStrProp(pin, "DefaultValue")) + "}";
        }
    }
    return "{\"name\":" + JsonStr(NameOf(n)) + ",\"class\":" + JsonStr(NameOf(Api().GetClass(n))) + ",\"x\":" + std::to_string(GetIntProp(n, "NodePosX", 0))
         + ",\"y\":" + std::to_string(GetIntProp(n, "NodePosY", 0)) + ",\"pins\":[" + pinsJ + "]}";
}

// Noeud cree "a la FGraphNodeCreator" : NewObject(outer=graph, RF_Transactional) + AddNode + CreateNewGuid + PostPlacedNewNode + AllocateDefaultPins
static void* CreateNodeInGraph(void* graph, void* nodeClass)
{
    FNameRaw none{ 0, 0, 0 };
    void* node = Api4().StaticConstructObject(nodeClass, graph, &none, 0x8 /*RF_Transactional*/, nullptr, false, nullptr);
    if (!node) return nullptr;
    Api4().GraphAddNode(graph, node, false, false);
    Api4().CreateNewGuid(node);
    CallVirtVoid(node, VT_PostPlacedNewNode);
    CallVirtVoid(node, VT_AllocateDefaultPins);
    return node;
}

// Pins utilisateur (entree/sortie de fonction) : "inputs"/"outputs" = JSON "name:type;name:type" (type = category[:sub_object][!array][&ref])
// -> pour rester dans le mini-JSON plat, chaque pin est passee en objet plat prefixe : in0_name, in0_category, in0_sub_object, in0_is_array...
static int AddUserPins(void* node, const JsonObject& o, const char* prefix, void* owner, std::vector<std::string>& created, std::string& err)
{
    int n = 0;
    for (int i = 0; i < 64; ++i) {
        std::string pre = std::string(prefix) + std::to_string(i) + "_";
        const JsonValue* nm = JsonGet(o, (pre + "name").c_str());
        if (!nm || nm->str.empty()) break;
        PinTypeBuf pt;
        if (!BuildPinType(o, pre.c_str(), owner, pt, err)) return -1;
        FStringTmp name(nm->str);
        void* pin = Api4().CreateUserDefinedPin(node, &name.s, pt.data);
        if (!pin) { err = "CreateUserDefinedPin refused '" + nm->str + "' (name already used ?)"; return -1; }
        created.push_back(nm->str); ++n;
    }
    return n;
}

// ------------------------------------------------------------------------------------------------------------
static bool Phase4Command(Command* c)
{
    if (c->cmd == "add_variable") {
        // {"cmd":"add_variable","blueprint":P,"name":N,"category":"int","sub_object":"/Script/Engine.Actor"(opt),"is_array":false,
        //  "default":"0"(opt),"replicated":false,"rep_notify":"OnRep_X"(opt),"var_category":"Relay"(opt),"flags_or":0(opt, uint64 CPF_*)}
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* nv = ArgStr(c->args, "name"); if (!nv) { c->reply = ErrReply(c, "missing name"); return true; }
        FNameRaw fn; MakeFName(Utf8ToWide(nv->str).c_str(), fn);
        if (Api4().FindNewVariableIndex(bp, &fn) >= 0) { c->reply = ErrReply(c, "variable already exists: " + nv->str); return true; }
        PinTypeBuf pt; std::string err;
        if (!BuildPinType(c->args, "", bp, pt, err)) { c->reply = ErrReply(c, err); return true; }
        const JsonValue* dv = JsonGet(c->args, "default");
        std::string def = dv ? (dv->isNum ? std::to_string(dv->num) : dv->isBool ? (dv->b ? "true" : "false") : dv->str) : "";
        FStringTmp defStr(def);
        bool added = Api4().AddMemberVariable(bp, &fn, pt.data, &defStr.s);
        if (!added) { c->reply = ErrReply(c, "AddMemberVariable returned false (name invalid or already used by a parent ?)"); return true; }
        uint64_t* pf = Api4().GetVarPropertyFlags(bp, &fn);
        uint64_t before = pf ? *pf : 0;
        // CPF_Net = 0x20, CPF_RepNotify = 0x100000000 (ObjectBase.h 4.5 ; CPF_Net confirme sur les variables repliquees de S+)
        if (pf && ArgBool(c->args, "replicated", false)) *pf |= 0x20ull;
        if (const JsonValue* rn = ArgStr(c->args, "rep_notify")) {
            FNameRaw rnf; MakeFName(Utf8ToWide(rn->str).c_str(), rnf);
            Api4().SetVarRepNotifyFunc(bp, &fn, &rnf);
            if (pf) *pf |= 0x20ull | 0x100000000ull;
        }
        if (const JsonValue* fo = JsonGet(c->args, "flags_or")) if (fo->isNum && pf) *pf |= (uint64_t)fo->num;
        if (const JsonValue* rc = JsonGet(c->args, "rep_condition")) if (rc->isNum) { if (uint8_t* p = Api4().GetVarRepCondition(bp, &fn)) *p = (uint8_t)rc->num; }
        if (const JsonValue* cat = ArgStr(c->args, "var_category")) {
            FNameRaw cf; MakeFName(Utf8ToWide(cat->str).c_str(), cf);
            Api4().SetVarCategory(bp, &fn, nullptr, &cf, true);
        }
        Api4().MarkBlueprintAsStructurallyModified(bp);
        Api3().MarkPackageDirty(bp);
        int idx = Api4().FindNewVariableIndex(bp, &fn);
        char fb[40]; sprintf_s(fb, "%llu", (unsigned long long)(pf ? *pf : 0));
        FileLog("add_variable: %s += %s (%s) idx %d flags %s", PathNameOf(bp).c_str(), nv->str.c_str(), def.c_str(), idx, fb);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"blueprint\":" + JsonStr(PathNameOf(bp)) + ",\"name\":" + JsonStr(nv->str)
            + ",\"index\":" + std::to_string(idx) + ",\"property_flags\":" + fb + ",\"flags_before\":" + std::to_string((unsigned long long)before) + "}");
        return true;
    }
    if (c->cmd == "remove_variable") {
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* nv = ArgStr(c->args, "name"); if (!nv) { c->reply = ErrReply(c, "missing name"); return true; }
        FNameRaw fn; MakeFName(Utf8ToWide(nv->str).c_str(), fn);
        if (Api4().FindNewVariableIndex(bp, &fn) < 0) { c->reply = ErrReply(c, "variable not found: " + nv->str); return true; }
        Api4().RemoveMemberVariable(bp, &fn);
        Api3().MarkPackageDirty(bp);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"removed\":" + JsonStr(nv->str) + "}");
        return true;
    }
    if (c->cmd == "add_function") {
        // {"cmd":"add_function","blueprint":P,"name":N,"override":false,"in0_name":..,"in0_category":..,"out0_name":..,"out0_category":..}
        // override=true : N doit etre une fonction (BlueprintImplementableEvent/NativeEvent avec sortie) de la classe parente ->
        // graph de surcharge avec entree+sortie generees depuis la UFunction (pins in/out ignores).
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* nv = ArgStr(c->args, "name"); if (!nv) { c->reply = ErrReply(c, "missing name"); return true; }
        bool over = ArgBool(c->args, "override", false);
        FNameRaw fn; MakeFName(Utf8ToWide(nv->str).c_str(), fn);
        // Surcharge : le nom existe forcement chez le parent -> IsGraphNameUnique dit non (constate 2026-09-04) ; on verifie
        // seulement qu'aucun graph du Blueprint ne porte deja ce nom (= deja surchargee).
        std::string existingKind;
        if (over ? (FindGraph(bp, nv->str, existingKind) != nullptr) : !Api4().IsGraphNameUnique(bp, &fn)) {
            c->reply = ErrReply(c, over ? "function already overridden in blueprint (graph exists): " + nv->str : "graph name not unique in blueprint: " + nv->str); return true;
        }
        void* func = nullptr;
        if (over) {
            void* parent = ReadObjectProp(bp, "ParentClass");
            if (!parent) { c->reply = ErrReply(c, "blueprint has no ParentClass"); return true; }
            func = Api4().FindFunctionByName(parent, &fn, 1 /*IncludeSuper*/);
            if (!func) { c->reply = ErrReply(c, "function not found on parent " + PathNameOf(parent) + ": " + nv->str); return true; }
        }
        void* graph = Api4().CreateNewGraph(bp, &fn, Api4().UEdGraphClass(), Api4().K2SchemaClass(), false);
        if (!graph) { c->reply = ErrReply(c, "CreateNewGraph returned null"); return true; }
        TArrayRaw* fg = ObjArrayProp(bp, "FunctionGraphs");
        if (!fg || !TArrayPtrAdd(fg, graph)) { c->reply = ErrReply(c, "cannot append to Blueprint.FunctionGraphs"); return true; }
        // == FBlueprintEditorUtils::AddFunctionGraph<T>(bp, graph, bIsUserCreated=!override, SignatureFromObject) (template non exporte, reproduit)
        Api4().ValidateBlueprintChildVariables(bp, &fn);
        void* schema = Api3().GetDefaultObject(Api4().K2SchemaClass(), true);
        if (!schema) { c->reply = ErrReply(c, "K2 schema CDO null"); return true; }
        // Surcharge : passer la CLASSE proprietaire (voie ImplementFunction de l'editeur) -> l'entree ET la sortie recoivent
        // SignatureClass=<classe> + SignatureName (comme les T3D S+). La surcharge UFunction* laisse SignatureClass=NULL et le
        // compilateur repond "The function name in node X is already used" (constate 2026-09-04).
        if (over) Api4().CreateTerminatorsClass(schema, graph, Api3().GetOuterObj(func));
        else {
            Api4().CreateTerminatorsClass(schema, graph, nullptr);
            Api4().AddExtraFunctionFlags(schema, graph, 0x04000000 | 0x08000000 | 0x200);   // FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public (= ExtraFlags 201457664 lu dans les T3D S+)
            Api4().MarkFunctionEntryAsEditable(schema, graph, true);
        }
        Api4().MarkBlueprintAsStructurallyModified(bp);
        void* entry = FindNodeOfClass(graph, Api4().K2FunctionEntryClass(L"/Script/BlueprintGraph"));
        void* result = FindNodeOfClass(graph, Api4().K2FunctionResultClass(L"/Script/BlueprintGraph"));
        std::vector<std::string> ins, outs; std::string err; bool pinsOk = true;
        if (!over) {
            if (entry && AddUserPins(entry, c->args, "in", bp, ins, err) < 0) pinsOk = false;
            if (pinsOk && JsonGet(c->args, "out0_name") && !result) {
                result = CreateNodeInGraph(graph, Api4().K2FunctionResultClass(L"/Script/BlueprintGraph"));
                if (result) { SetIntProp(result, "NodePosX", (entry ? GetIntProp(entry, "NodePosX", 0) : 0) + 480); SetIntProp(result, "NodePosY", entry ? GetIntProp(entry, "NodePosY", 0) : 0); }
            }
            if (pinsOk && result && AddUserPins(result, c->args, "out", bp, outs, err) < 0) pinsOk = false;
            if (!ins.empty() || !outs.empty()) Api4().MarkBlueprintAsStructurallyModified(bp);
        }
        Api3().MarkPackageDirty(bp);
        FileLog("add_function: %s += %s (override=%d entry=%p result=%p in=%zu out=%zu) %s", PathNameOf(bp).c_str(), nv->str.c_str(), over, entry, result, ins.size(), outs.size(), err.c_str());
        std::string insJ, outsJ; for (auto& s : ins) insJ += (insJ.empty() ? "" : ",") + JsonStr(s); for (auto& s : outs) outsJ += (outsJ.empty() ? "" : ",") + JsonStr(s);
        c->reply = MakeReply(c->idJson, pinsOk, "\"result\":{\"graph\":" + JsonStr(PathNameOf(graph)) + ",\"override\":" + JsonBool(over)
            + ",\"function\":" + JsonStr(func ? PathNameOf(func) : "") + ",\"entry_node\":" + JsonStr(entry ? NameOf(entry) : "")
            + ",\"result_node\":" + JsonStr(result ? NameOf(result) : "") + ",\"inputs\":[" + insJ + "],\"outputs\":[" + outsJ + "]"
            + (pinsOk ? "" : ",\"error\":" + JsonStr(err)) + "}");
        return true;
    }
    if (c->cmd == "remove_graph") {
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* gv = ArgStr(c->args, "graph"); if (!gv) { c->reply = ErrReply(c, "missing graph"); return true; }
        std::string kind; void* g = FindGraph(bp, gv->str, kind);
        if (!g) { c->reply = ErrReply(c, "graph not found: " + gv->str); return true; }
        if (kind == "UbergraphPages") { c->reply = ErrReply(c, "refusing to remove an ubergraph page"); return true; }
        Api4().RemoveGraph(bp, g, 3 /*EGraphRemoveFlags::Default = MarkBlueprintAsStructurallyModified|Recompile*/);
        Api3().MarkPackageDirty(bp);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"removed\":" + JsonStr(gv->str) + ",\"from\":" + JsonStr(kind) + "}");
        return true;
    }
    if (c->cmd == "paste_t3d") {
        // {"cmd":"paste_t3d","blueprint":P,"graph":"EventGraph"|"MaFonction"|chemin,"t3d":"Begin Object Class=K2Node_... End Object ...",
        //  "reconstruct":true,"offset_x":0,"offset_y":0}
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* gv = ArgStr(c->args, "graph"); const JsonValue* tv = ArgStr(c->args, "t3d");
        if (!gv || !tv) { c->reply = ErrReply(c, "missing graph or t3d"); return true; }
        std::string kind; void* graph = FindGraph(bp, gv->str, kind);
        if (!graph) { c->reply = ErrReply(c, "graph not found: " + gv->str); return true; }
        // Pre-controle : blocs de premier niveau = noeuds (pas de wrapper EdGraph), noms non deja pris dans le graph
        {
            std::string t = tv->str; size_t pos = 0; int top = 0;
            while (pos < t.size()) {
                size_t nl = t.find('\n', pos); if (nl == std::string::npos) nl = t.size();
                std::string line = t.substr(pos, nl - pos); pos = nl + 1;
                if (line.compare(0, 19, "Begin Object Class=") != 0) continue;
                ++top;
                if (line.find("Class=EdGraph ") != std::string::npos) { c->reply = ErrReply(c, "t3d must contain node blocks only (no 'Begin Object Class=EdGraph' wrapper)"); return true; }
                size_t q = line.find("Name=\""); if (q == std::string::npos) continue;
                size_t e = line.find('"', q + 6); std::string nm = line.substr(q + 6, e == std::string::npos ? std::string::npos : e - q - 6);
                if (Api().StaticFindObject(nullptr, graph, Utf8ToWide(nm).c_str(), false)) { c->reply = ErrReply(c, "object name already exists in graph: " + nm); return true; }
            }
            if (top == 0) { c->reply = ErrReply(c, "no 'Begin Object Class=...' block at column 0"); return true; }
        }
        std::string log; FCaptureDevice dev{ g_captureVtable, false, true, &log, 0, false };
        void* redirector = Api4().GetRedirector();
        FStringTmp text(tv->str);
        uint8_t setBuf[256]; memset(setBuf, 0, sizeof(setBuf));   // TSet<UEdGraphNode*> vide (zero-init valide, cf. en-tete)
        if (redirector) Api4().AddOutputDevice(redirector, &dev);
        Api4().ImportNodesFromText(graph, &text.s, setBuf);
        if (redirector) Api4().RemoveOutputDevice(redirector, &dev);
        TArrayRaw* elems = (TArrayRaw*)setBuf;
        std::vector<void*> nodes;
        for (int i = 0; i < elems->Num; ++i) { void* n = *(void**)((uint8_t*)elems->Data + i * 16); if (n) nodes.push_back(n); }
        bool reconstruct = ArgBool(c->args, "reconstruct", true);
        int ox = ArgInt(c->args, "offset_x", 0), oy = ArgInt(c->args, "offset_y", 0);
        std::string nodesJ;
        for (void* n : nodes) {
            Api4().CreateNewGuid(n);
            CallVirtVoid(n, VT_PostPasteNode);
            if (ox || oy) { SetIntProp(n, "NodePosX", GetIntProp(n, "NodePosX", 0) + ox); SetIntProp(n, "NodePosY", GetIntProp(n, "NodePosY", 0) + oy); }
            if (reconstruct && Api3().IsA(n, Api4().K2NodeClass(L"/Script/BlueprintGraph"))) CallVirtVoid(n, VT_ReconstructNode);
            TArrayRaw* pins = ObjArrayProp(n, "Pins");
            nodesJ += (nodesJ.empty() ? "" : ",") + std::string("{\"name\":") + JsonStr(NameOf(n)) + ",\"class\":" + JsonStr(NameOf(Api().GetClass(n)))
                    + ",\"pins\":" + std::to_string(pins ? pins->Num : -1) + "}";
        }
        // liberation du TSet (Data toujours ; buffers secondaires seulement si le layout s'auto-verifie : NumBits == Num)
        int32_t numBits = *(int32_t*)(setBuf + 40);
        if (elems->Data) Alloc().Free(elems->Data);
        if (numBits == elems->Num && elems->Num > 0) {
            void* flagsSecondary = *(void**)(setBuf + 32); void* hashSecondary = *(void**)(setBuf + 64);
            if (flagsSecondary) Alloc().Free(flagsSecondary);
            if (hashSecondary) Alloc().Free(hashSecondary);
        }
        Api4().NotifyGraphChanged(graph);
        Api4().MarkBlueprintAsStructurallyModified(bp);
        Api3().MarkPackageDirty(bp);
        if (log.size() > 8000) log.resize(8000);
        FileLog("paste_t3d: %s/%s += %zu noeuds (%zu car. t3d, reconstruct=%d)", PathNameOf(bp).c_str(), gv->str.c_str(), nodes.size(), tv->str.size(), reconstruct);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"graph\":" + JsonStr(PathNameOf(graph)) + ",\"count\":" + std::to_string(nodes.size())
            + ",\"nodes\":[" + nodesJ + "],\"import_log\":" + JsonStr(log) + "}");
        return true;
    }
    if (c->cmd == "list_nodes") {
        // {"cmd":"list_nodes","blueprint":P,"graph":"EventGraph"}  (lecture seule, pas de liste blanche)
        if (!P4Ready(c)) return true;
        const JsonValue* bv = ArgStr(c->args, "blueprint"); const JsonValue* gv = ArgStr(c->args, "graph");
        if (!bv || !gv) { c->reply = ErrReply(c, "missing blueprint or graph"); return true; }
        void* bp = ResolveObject(bv->str, true);
        if (!bp || !Api3().IsA(bp, Api3().UBlueprintClass())) { c->reply = ErrReply(c, "blueprint not found: " + bv->str); return true; }
        std::string kind; void* graph = FindGraph(bp, gv->str, kind);
        if (!graph) { c->reply = ErrReply(c, "graph not found: " + gv->str); return true; }
        std::string nodesJ; int count = 0;
        if (TArrayRaw* nodes = ObjArrayProp(graph, "Nodes"))
            for (int i = 0; i < nodes->Num; ++i) { void* n = ((void**)nodes->Data)[i]; if (!n) continue; nodesJ += (nodesJ.empty() ? "" : ",") + NodeJson(n); ++count; }
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"graph\":" + JsonStr(PathNameOf(graph)) + ",\"kind\":" + JsonStr(kind) + ",\"count\":" + std::to_string(count) + ",\"nodes\":[" + nodesJ + "]}");
        return true;
    }
    if (c->cmd == "link_pins") {
        // {"cmd":"link_pins","blueprint":P,"graph":G,"from":"K2Node_FunctionEntry_0","from_pin":"then","to":"K2Node_CallFunction_1","to_pin":"execute","force":false}
        // TryCreateConnection (schema K2 : validation des types, casse un lien exec existant) ; force=true -> MakeLinkTo sans validation
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* gv = ArgStr(c->args, "graph"); const JsonValue* fa = ArgStr(c->args, "from"); const JsonValue* fp = ArgStr(c->args, "from_pin");
        const JsonValue* ta = ArgStr(c->args, "to"); const JsonValue* tp = ArgStr(c->args, "to_pin");
        if (!gv || !fa || !fp || !ta || !tp) { c->reply = ErrReply(c, "missing graph/from/from_pin/to/to_pin"); return true; }
        std::string kind; void* graph = FindGraph(bp, gv->str, kind);
        if (!graph) { c->reply = ErrReply(c, "graph not found: " + gv->str); return true; }
        void* na = FindNodeByName(graph, fa->str); void* nb = FindNodeByName(graph, ta->str);
        if (!na) { c->reply = ErrReply(c, "from node not found in graph: " + fa->str); return true; }
        if (!nb) { c->reply = ErrReply(c, "to node not found in graph: " + ta->str); return true; }
        void* pa = FindPinByName(na, fp->str); void* pb = FindPinByName(nb, tp->str);
        if (!pa) { c->reply = ErrReply(c, "from pin not found: " + fa->str + "." + fp->str); return true; }
        if (!pb) { c->reply = ErrReply(c, "to pin not found: " + ta->str + "." + tp->str); return true; }
        bool force = ArgBool(c->args, "force", false), linked;
        if (force) { Api4().PinMakeLinkTo(pa, pb); linked = true; }
        else { void* schema = Api3().GetDefaultObject(Api4().K2SchemaClass(), true); linked = Api4().TryCreateConnection(schema, pa, pb); }
        TArrayRaw* la = ObjArrayProp(pa, "LinkedTo");
        Api4().NotifyGraphChanged(graph); Api3().MarkPackageDirty(bp);
        FileLog("link_pins: %s.%s -> %s.%s : %d", fa->str.c_str(), fp->str.c_str(), ta->str.c_str(), tp->str.c_str(), linked);
        c->reply = MakeReply(c->idJson, linked, "\"result\":{\"linked\":" + JsonBool(linked) + ",\"from_links\":" + std::to_string(la ? la->Num : -1)
            + (linked ? "" : ",\"error\":\"TryCreateConnection refused (types incompatibles ?) ; force=true pour MakeLinkTo\"") + "}");
        return true;
    }
    if (c->cmd == "set_pin_default") {
        // {"cmd":"set_pin_default","blueprint":P,"graph":G,"node":"K2Node_FunctionResult_0","pin":"ReturnValue","value":"true"} ou "object":"/Game/...X_C"
        if (!P4Ready(c)) return true;
        void* bp = P4Blueprint(c); if (!bp) return true;
        const JsonValue* gv = ArgStr(c->args, "graph"); const JsonValue* nv = ArgStr(c->args, "node"); const JsonValue* pv = ArgStr(c->args, "pin");
        if (!gv || !nv || !pv) { c->reply = ErrReply(c, "missing graph/node/pin"); return true; }
        std::string kind; void* graph = FindGraph(bp, gv->str, kind);
        if (!graph) { c->reply = ErrReply(c, "graph not found: " + gv->str); return true; }
        void* node = FindNodeByName(graph, nv->str); if (!node) { c->reply = ErrReply(c, "node not found: " + nv->str); return true; }
        void* pin = FindPinByName(node, pv->str); if (!pin) { c->reply = ErrReply(c, "pin not found: " + nv->str + "." + pv->str); return true; }
        void* schema = Api3().GetDefaultObject(Api4().K2SchemaClass(), true);
        if (const JsonValue* ov = ArgStr(c->args, "object")) {
            void* obj = ResolveObject(ov->str, true); if (!obj) { c->reply = ErrReply(c, "object not found: " + ov->str); return true; }
            Api4().TrySetDefaultObject(schema, pin, obj);
        } else {
            const JsonValue* vv = JsonGet(c->args, "value"); if (!vv) { c->reply = ErrReply(c, "missing value or object"); return true; }
            std::string v = vv->isNum ? std::to_string(vv->num) : vv->isBool ? (vv->b ? "true" : "false") : vv->str;
            FStringTmp s(v); Api4().TrySetDefaultValue(schema, pin, &s.s);
        }
        Api3().MarkPackageDirty(bp);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"node\":" + JsonStr(nv->str) + ",\"pin\":" + JsonStr(pv->str) + ",\"default\":" + JsonStr(GetStrProp(pin, "DefaultValue"))
            + ",\"default_object\":" + JsonStr(ExportProp(pin, "DefaultObject")) + "}");
        return true;
    }
    if (c->cmd == "pie_start" || c->cmd == "pie_stop") {
        // {"cmd":"pie_start","at_player_start":true,"simulate":false} : demande une session PIE sur la map ouverte (traitee au tick
        // suivant par StartQueuedPlayMapRequest, nouvelle fenetre). {"cmd":"pie_stop"} : RequestEndPlayMap.
        if (!P4Ready(c)) return true;
        void* editor = GEditorPtr(); if (!editor) { c->reply = ErrReply(c, "GEditor null"); return true; }
        if (c->cmd == "pie_start") {
            TSharedPtrRaw noViewport{ nullptr, nullptr };
            Api4().RequestPlaySession(editor, ArgBool(c->args, "at_player_start", true), &noViewport, ArgBool(c->args, "simulate", false), nullptr, nullptr, -1, false);
            FileLog("pie_start demande");
        } else { Api4().RequestEndPlayMap(editor); FileLog("pie_stop demande"); }
        c->reply = MakeReply(c->idJson, true, "\"result\":\"requested\"");
        return true;
    }
    if (c->cmd == "console_command") {
        // {"cmd":"console_command","world_path":"/Game/Maps/UEDPIE_0_X.X","command":"cheat summon Y_C"} -> commande console du 1er PlayerController
        // du monde (= ce que tape le joueur dans la console PIE : prefixe 'cheat' d'ARK compris). Renvoie la sortie FString.
        if (!P4Ready(c)) return true;
        const JsonValue* wv = ArgStr(c->args, "world_path"); const JsonValue* cv = ArgStr(c->args, "command");
        if (!wv || !cv) { c->reply = ErrReply(c, "missing world_path or command"); return true; }
        void* world = ResolveObject(wv->str, false);
        if (!world) { c->reply = ErrReply(c, "world not found (not loaded): " + wv->str); return true; }
        void* pc = Api4().GetFirstPlayerController(world);
        if (!pc) { c->reply = ErrReply(c, "no PlayerController in world " + wv->str); return true; }
        FStringTmp cmd(cv->str); FStringRaw out{};
        Api4().PCConsoleCommand(pc, &out, &cmd.s, true);
        std::string res = TakeFString(out);
        FileLog("console_command [%s] %s -> %s", NameOf(pc).c_str(), cv->str.c_str(), res.c_str());
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"player_controller\":" + JsonStr(PathNameOf(pc)) + ",\"output\":" + JsonStr(res) + "}");
        return true;
    }
    if (c->cmd == "compile_blueprint") {
        // {"cmd":"compile_blueprint","blueprint":P}  -> statut + messages du compilateur (LogBlueprint "[compiler X] ..." captures via AddOutputDevice)
        if (!P4Ready(c)) return true;
        const JsonValue* bv = ArgStr(c->args, "blueprint");
        if (!bv) { c->reply = ErrReply(c, "missing blueprint"); return true; }
        void* bp = ResolveObject(bv->str, true);
        if (!bp) { c->reply = ErrReply(c, "blueprint not found: " + bv->str); return true; }
        if (!Api3().IsA(bp, Api3().UBlueprintClass())) { c->reply = ErrReply(c, "not a UBlueprint: " + PathNameOf(bp)); return true; }
        std::string log; FCaptureDevice dev{ g_captureVtable, false, true, &log, 0, false };
        dev.keepVerbosity = true;
        void* redirector = Api4().GetRedirector();
        DWORD t0 = GetTickCount();
        if (redirector) Api4().AddOutputDevice(redirector, &dev);
        Api3().CompileBlueprint(bp, false, false, false, nullptr);
        if (redirector) Api4().RemoveOutputDevice(redirector, &dev);
        PropInfo st; int status = -1;
        if (FindProp(bp, "Status", st) && st.size == 1) status = *((uint8_t*)bp + st.offset);
        static const char* kStatus[] = { "Unknown", "Dirty", "Error", "UpToDate", "BeingCreated", "UpToDateWithWarnings" };
        // lignes capturees : "<verbosite>\t<texte>" ; on garde erreurs/avertissements + tout ce qui vient du compilateur
        std::string msgs; int nErr = 0, nWarn = 0; size_t pos = 0;
        while (pos < log.size()) {
            size_t nl = log.find('\n', pos); if (nl == std::string::npos) nl = log.size();
            std::string line = log.substr(pos, nl - pos); pos = nl + 1;
            size_t tab = line.find('\t'); if (tab == std::string::npos) continue;
            int verb = atoi(line.substr(0, tab).c_str()); std::string text = line.substr(tab + 1);
            bool compiler = text.find("[compiler") != std::string::npos;
            if (verb == 2) ++nErr; else if (verb == 3) ++nWarn;
            if (verb <= 3 || compiler) {
                msgs += (msgs.empty() ? "" : ",") + std::string("{\"severity\":") + JsonStr(verb == 2 ? "error" : verb == 3 ? "warning" : verb == 1 ? "fatal" : "info")
                      + ",\"text\":" + JsonStr(text) + "}";
            }
        }
        void* gen = ReadObjectProp(bp, "GeneratedClass");
        FileLog("compile_blueprint: %s -> status %d, %d erreurs, %d avertissements (%lu ms)", bv->str.c_str(), status, nErr, nWarn, GetTickCount() - t0);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"blueprint\":" + JsonStr(PathNameOf(bp)) + ",\"status\":" + std::to_string(status)
            + ",\"status_name\":" + JsonStr(status >= 0 && status <= 5 ? kStatus[status] : "?") + ",\"generated_class\":" + JsonStr(gen ? PathNameOf(gen) : "")
            + ",\"errors\":" + std::to_string(nErr) + ",\"warnings\":" + std::to_string(nWarn) + ",\"messages\":[" + msgs + "]"
            + ",\"ms\":" + std::to_string(GetTickCount() - t0) + "}");
        return true;
    }
    return false;
}
