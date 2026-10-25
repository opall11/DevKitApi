// DevKitApi v0 — coquille : plugin DLL charge par le DevKit ARK (UE 4.5.1 Wildcard).
// Expose un socket TCP 127.0.0.1:9877 (une requete JSON par ligne, une reponse JSON par ligne).
// Regles (BRIEF.md §2) : aucun appel virtuel sur objet moteur, memoire via FMemory, UObjects sur le
// thread principal uniquement (le thread socket ne fait que mettre en file), SEH autour de chaque commande.
// Toutes les fonctions moteur sont resolues par GetProcAddress (zero lien avec UE4Editor-*.dll).
// Preuves des signatures/layouts : refs\exports_utiles.md, refs\layouts.md.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "user32.lib")

// ---------------------------------------------------------------------------------------------
// Journal fichier (toujours dispo, meme si le moteur n'est pas encore pret)
// ---------------------------------------------------------------------------------------------
static HINSTANCE g_hInstance = nullptr;   // pose dans DllMain ; sert a localiser la DLL (journal, liste blanche, racine Content)

// Dossier de la DLL (ex: <DevKit>/Engine/Plugins/DevKitApi/Binaries/Win64/), avec le separateur final. Vide si inconnu.
static std::wstring DllDir()
{
    static std::wstring dir;
    if (dir.empty() && g_hInstance) {
        wchar_t buf[MAX_PATH] = {};
        if (GetModuleFileNameW(g_hInstance, buf, MAX_PATH)) {
            std::wstring w = buf; size_t slash = w.find_last_of(L"\\/");
            if (slash != std::wstring::npos) dir = w.substr(0, slash + 1);
        }
    }
    return dir;
}

// Journal : devkitapi.log A COTE DE LA DLL (plus de chemin en dur).
static const wchar_t* FileLogPath()
{
    static std::wstring path;
    if (path.empty()) path = DllDir() + L"devkitapi.log";
    return path.c_str();
}

static void FileLog(const char* fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SYSTEMTIME st; GetLocalTime(&st);
    FILE* f = nullptr;
    if (_wfopen_s(&f, FileLogPath(), L"ab") == 0 && f) {
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d.%03d [tid %lu] %s\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                GetCurrentThreadId(), buf);
        fclose(f);
    }
    OutputDebugStringA(buf); OutputDebugStringA("\n");
}

// ---------------------------------------------------------------------------------------------
// Exports moteur resolus a la volee (noms mangles verifies dans exports\*.txt)
// ---------------------------------------------------------------------------------------------
static FARPROC Resolve(const wchar_t* dll, const char* sym)
{
    HMODULE h = GetModuleHandleW(dll);
    return h ? GetProcAddress(h, sym) : nullptr;
}

// FMemory::Malloc(SIZE_T, uint32 Alignment) / FMemory::Free(void*)      [Core]
typedef void* (*FMemoryMalloc_t)(size_t Count, unsigned int Alignment);
typedef void  (*FMemoryFree_t)(void* Ptr);

// Resolution unique. Si Core n'est pas la (impossible en pratique : c'est Core qui nous charge),
// on retombe sur le CRT de facon COHERENTE (meme allocateur pour new et delete).
struct UEAlloc {
    FMemoryMalloc_t Malloc = nullptr;
    FMemoryFree_t   Free   = nullptr;
    bool            useCrt = false;
    UEAlloc() {
        Malloc = (FMemoryMalloc_t)Resolve(L"UE4Editor-Core.dll", "?Malloc@FMemory@@SAPEAX_KI@Z");
        Free   = (FMemoryFree_t)  Resolve(L"UE4Editor-Core.dll", "?Free@FMemory@@SAXPEAX@Z");
        useCrt = (!Malloc || !Free);
    }
};
static UEAlloc& Alloc() { static UEAlloc a; return a; }   // init au 1er appel (pas dans DllMain)

// operator new / delete globaux de la DLL -> FMemory (equivalent du PER_MODULE_BOILERPLATE d'UE4).
// L'objet module est detruit par le moteur via notre destructeur virtuel -> passe par ce delete.
void* operator new(size_t n)                          { UEAlloc& a = Alloc(); return a.useCrt ? malloc(n ? n : 1) : a.Malloc(n, 0); }
void* operator new[](size_t n)                        { UEAlloc& a = Alloc(); return a.useCrt ? malloc(n ? n : 1) : a.Malloc(n, 0); }
void  operator delete(void* p) noexcept               { if (!p) return; UEAlloc& a = Alloc(); a.useCrt ? free(p) : a.Free(p); }
void  operator delete[](void* p) noexcept             { if (!p) return; UEAlloc& a = Alloc(); a.useCrt ? free(p) : a.Free(p); }
void  operator delete(void* p, size_t) noexcept       { operator delete(p); }
void  operator delete[](void* p, size_t) noexcept     { operator delete[](p); }

// Journal Output Log :
//   FOutputDeviceRedirector* FOutputDeviceRedirector::Get()                           [Core 0xBB3F0]  (= GLog, non exporte)
//   void FOutputDevice::Log(const FName& Category, ELogVerbosity::Type, const TCHAR*)  [Core 0x214B40] (non virtuelle)
//   FName::FName(const TCHAR*, EFindName, bool bSplitName)                             [Core 0x2533E0]
typedef void* (*GetRedirector_t)();
typedef void  (*OutputDeviceLogCat_t)(void* self, const void* category, int verbosity, const wchar_t* text);
typedef void* (*FNameCtorW_t)(void* self, const wchar_t* name, int findType, bool bSplitName);
enum { ELogVerbosity_Warning = 3, ELogVerbosity_Log = 5 };
enum { FNAME_Add = 1 };

struct FNameRaw { int32_t ComparisonIndex, DisplayIndex, Number; };  // 12 octets (layouts.md)

static void EngineLog(int verbosity, const wchar_t* text)
{
    static GetRedirector_t      getLog = (GetRedirector_t)Resolve(L"UE4Editor-Core.dll", "?Get@FOutputDeviceRedirector@@SAPEAV1@XZ");
    static OutputDeviceLogCat_t logCat = (OutputDeviceLogCat_t)Resolve(L"UE4Editor-Core.dll", "?Log@FOutputDevice@@QEAAXAEBVFName@@W4Type@ELogVerbosity@@PEB_W@Z");
    static FNameCtorW_t         fnameCtor = (FNameCtorW_t)Resolve(L"UE4Editor-Core.dll", "??0FName@@QEAA@PEB_WW4EFindName@@_N@Z");
    static FNameRaw category = { 0, 0, 0 };
    static bool categoryInit = false;
    if (!getLog || !logCat) return;
    if (!categoryInit && fnameCtor) { fnameCtor(&category, L"LogDevKitApi", FNAME_Add, true); categoryInit = true; }
    void* redirector = getLog();
    if (redirector) logCat(redirector, &category, verbosity, text);
}

// Etat de l'editeur (lecture de pointeurs/bools : sans risque depuis n'importe quel thread)
//   UEditorEngine* GEditor   [UnrealEd, data]     bool GIsRunning [Core, data]   bool GIsRequestingExit [Core, data]
static bool EditorReady()
{
    static void** pGEditor = (void**)Resolve(L"UE4Editor-UnrealEd.dll", "?GEditor@@3PEAVUEditorEngine@@EA");
    static bool*  pGIsRunning = (bool*)Resolve(L"UE4Editor-Core.dll", "?GIsRunning@@3_NA");
    if (!pGEditor) pGEditor = (void**)Resolve(L"UE4Editor-UnrealEd.dll", "?GEditor@@3PEAVUEditorEngine@@EA");
    return pGEditor && *pGEditor && pGIsRunning && *pGIsRunning;
}
static void* GEditorPtr()
{
    static void** p = nullptr;
    if (!p) p = (void**)Resolve(L"UE4Editor-UnrealEd.dll", "?GEditor@@3PEAVUEditorEngine@@EA");
    return p ? *p : nullptr;
}
// GWorld est un UWorldProxy (`?GWorld@@3VUWorldProxy@@A`, Engine) dont le 1er membre est `UWorld* World`
// (source 4.5 : class UWorldProxy { UWorld* World; }). Non prouve en Ghidra -> utilise seulement si demande.
static void* GWorldPtr()
{
    static void** p = nullptr;
    if (!p) p = (void**)Resolve(L"UE4Editor-Engine.dll", "?GWorld@@3VUWorldProxy@@A");
    return p ? *p : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Acces objets (CoreUObject / UnrealEd) — signatures : refs\exports_utiles.md
// ---------------------------------------------------------------------------------------------
static std::string JsonEscape(const std::string& s);   // defini plus bas (section JSON)
static std::wstring Utf8ToWide(const std::string& s);  // idem
struct FStringRaw { wchar_t* Data; int32_t Num; int32_t Max; };   // 16 octets (layouts.md)
static void* const ANY_PACKAGE = (void*)(intptr_t)-1;

// UObject* StaticFindObject(UClass* Class, UObject* Outer, const TCHAR* Name, bool ExactClass)
typedef void* (*StaticFindObject_t)(void* Class, void* Outer, const wchar_t* Name, bool ExactClass);
// UObject* StaticLoadObject(UClass*, UObject* Outer, const TCHAR* Name, const TCHAR* Filename, uint32 LoadFlags, UPackageMap*, bool bAllowObjectReconciliation)
typedef void* (*StaticLoadObject_t)(void* Class, void* Outer, const wchar_t* Name, const wchar_t* Filename, unsigned LoadFlags, void* Sandbox, bool bAllowReconcile);
// FString UObjectBaseUtility::GetPathName(const UObject* StopOuter) const  -> this=rcx, retour cache=rdx, StopOuter=r8
typedef FStringRaw* (*GetPathName_t)(const void* self, FStringRaw* out, const void* StopOuter);
// FString UObjectBaseUtility::GetName() const
typedef FStringRaw* (*GetName_t)(const void* self, FStringRaw* out);
typedef void* (*GetClass_t)(const void* self);          // UClass* UObjectBase::GetClass() const  ([this+0x10])
typedef int   (*GetFlags_t)(const void* self);          // EObjectFlags UObjectBase::GetFlags() const ([this+8])
// bool UUnrealEdEngine::Exec(UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)   (classe reelle de GEditor)
typedef bool  (*EditorExec_t)(void* self, void* World, const wchar_t* Cmd, void* Ar);

struct EngineApi {
    StaticFindObject_t StaticFindObject = nullptr;
    StaticLoadObject_t StaticLoadObject = nullptr;
    GetPathName_t      GetPathName = nullptr;
    GetName_t          GetName = nullptr;
    GetClass_t         GetClass = nullptr;
    GetFlags_t         GetFlags = nullptr;
    EditorExec_t       EditorExec = nullptr;
    void*              UObjectClass = nullptr;   // UClass* de UObject (cache)
    bool               ok = false;
    void Init() {
        if (ok) return;
        StaticFindObject = (StaticFindObject_t)Resolve(L"UE4Editor-CoreUObject.dll", "?StaticFindObject@@YAPEAVUObject@@PEAVUClass@@PEAV1@PEB_W_N@Z");
        StaticLoadObject = (StaticLoadObject_t)Resolve(L"UE4Editor-CoreUObject.dll", "?StaticLoadObject@@YAPEAVUObject@@PEAVUClass@@PEAV1@PEB_W2IPEAVUPackageMap@@_N@Z");
        GetPathName      = (GetPathName_t)Resolve(L"UE4Editor-CoreUObject.dll", "?GetPathName@UObjectBaseUtility@@QEBA?AVFString@@PEBVUObject@@@Z");
        GetName          = (GetName_t)Resolve(L"UE4Editor-CoreUObject.dll", "?GetName@UObjectBaseUtility@@QEBA?AVFString@@XZ");
        GetClass         = (GetClass_t)Resolve(L"UE4Editor-CoreUObject.dll", "?GetClass@UObjectBase@@QEBAPEAVUClass@@XZ");
        GetFlags         = (GetFlags_t)Resolve(L"UE4Editor-CoreUObject.dll", "?GetFlags@UObjectBase@@QEBA?AW4EObjectFlags@@XZ");
        EditorExec       = (EditorExec_t)Resolve(L"UE4Editor-UnrealEd.dll", "?Exec@UUnrealEdEngine@@UEAA_NPEAVUWorld@@PEB_WAEAVFOutputDevice@@@Z");
        ok = StaticFindObject && StaticLoadObject && GetPathName && GetName && GetClass && GetFlags;
        if (!ok) FileLog("EngineApi: resolution incomplete (find=%p load=%p path=%p name=%p class=%p flags=%p exec=%p)",
                         StaticFindObject, StaticLoadObject, GetPathName, GetName, GetClass, GetFlags, EditorExec);
        if (ok && !UObjectClass) UObjectClass = StaticFindObject(nullptr, ANY_PACKAGE, L"/Script/CoreUObject.Object", false);
    }
};
static EngineApi& Api() { static EngineApi a; return a; }

static std::string WideToUtf8(const wchar_t* w, int len)
{
    if (!w || len <= 0) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w, len, nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, len, &s[0], n, nullptr, nullptr);
    return s;
}

// Recupere le contenu d'une FString rendue par le moteur puis libere son buffer via FMemory::Free
static std::string TakeFString(FStringRaw& s)
{
    std::string r = (s.Data && s.Num > 0) ? WideToUtf8(s.Data, s.Num - 1) : "";
    if (s.Data && !Alloc().useCrt) Alloc().Free(s.Data);
    s.Data = nullptr; s.Num = s.Max = 0;
    return r;
}

static std::string JsonStr(const std::string& s) { return "\"" + JsonEscape(s) + "\""; }

// {"found":bool,"path":..,"class":..,"name":..,"flags":N,"ptr":"0x.."}
static std::string ObjectInfoJson(void* obj)
{
    if (!obj) return "{\"found\":false}";
    EngineApi& a = Api();
    FStringRaw tmp{};
    a.GetPathName(obj, &tmp, nullptr);  std::string path = TakeFString(tmp);
    a.GetName(obj, &tmp);               std::string name = TakeFString(tmp);
    std::string cls;
    if (void* k = a.GetClass(obj)) { a.GetPathName(k, &tmp, nullptr); cls = TakeFString(tmp); }
    char ptr[32]; sprintf_s(ptr, "0x%p", obj);
    return "{\"found\":true,\"path\":" + JsonStr(path) + ",\"class\":" + JsonStr(cls) + ",\"name\":" + JsonStr(name)
         + ",\"flags\":" + std::to_string((unsigned)a.GetFlags(obj)) + ",\"ptr\":\"" + ptr + "\"}";
}

// FOutputDevice de capture : vtable reproduite (layouts.md : [0] dtor, [1] Serialize(Text,Verbosity,const FName&),
// [2] Flush, [3] TearDown, [4] CanBeUsedOnAnyThread). Le moteur n'appelle que ces slots (Logf/Log -> slot 1).
struct FCaptureDevice {
    void**       vtable;
    bool         bSuppressEventTag;        // +8  (layout FOutputDevice 4.5)
    bool         bAutoEmitLineTerminator;  // +9
    std::string* out;
    size_t       lines;
    bool         mirror;
    bool         keepVerbosity;   // phase 4 : prefixe chaque ligne de "<verbosite>\t" (capture des messages du compilateur)
};
static void CD_Dtor(void*, unsigned) {}
static void CD_Serialize(FCaptureDevice* self, const wchar_t* text, int verbosity, const void* /*category*/)
{
    if (!text) return;
    size_t n = wcslen(text);
    if (self->out && self->out->size() < (1u << 20)) {
        if (self->keepVerbosity) { self->out->append(std::to_string(verbosity)); self->out->push_back('\t'); }
        self->out->append(WideToUtf8(text, (int)n)); self->out->push_back('\n');
    }
    self->lines++;
    if (self->mirror) EngineLog(verbosity, text);
}
static void CD_Flush(void*) {}
static void CD_TearDown(void*) {}
static bool CD_CanBeUsedOnAnyThread(const void*) { return false; }
static void* g_captureVtable[8] = {
    (void*)CD_Dtor, (void*)CD_Serialize, (void*)CD_Flush, (void*)CD_TearDown, (void*)CD_CanBeUsedOnAnyThread,
    (void*)CD_Flush, (void*)CD_Flush, (void*)CD_Flush   // marge si un slot inattendu est appele
};

// ---------------------------------------------------------------------------------------------
// Phase 3 : reflexion, sauvegarde, duplication, blueprints (signatures : refs\exports_utiles.md)
// ---------------------------------------------------------------------------------------------
struct TSharedPtrRaw { void* Object; int32_t* Controller; };   // layouts.md (mode non thread-safe)

typedef void*          (*StaticClassFn_t)();
typedef void*          (*FindPropertyByName_t)(const void* ustruct, const FNameRaw* name);                 // FName par valeur = pointeur
typedef const wchar_t* (*ImportText_t)(const void* prop, const wchar_t* buffer, void* data, int portFlags, void* owner, void* errorDevice);
typedef bool           (*ExportText_InContainer_t)(const void* prop, int index, FStringRaw* out, const void* container, const void* delta, void* parent, int portFlags, void* rootScope);
typedef int            (*PropInt_t)(const void* prop);
typedef void*          (*GetDefaultObject_t)(void* cls, bool bCreateIfNeeded);
typedef void           (*ObjVoid_t)(void* obj);
typedef void*          (*GetOutermost_t)(const void* obj);
typedef void           (*SetFlags_t)(void* obj, int flags);
typedef bool           (*IsChildOf_t)(const void* ustruct, const void* other);
typedef bool           (*IsA_t)(const void* obj, const void* cls);
typedef void*          (*StaticDuplicateObject_t)(const void* src, void* destOuter, const wchar_t* destName, unsigned flagMask, void* destClass, int dupForPie);
typedef void*          (*CreatePackage_t)(void* outer, const wchar_t* name);
typedef bool           (*EditorSavePackage_t)(void* editor, void* pkg, void* base, unsigned topLevelFlags, const wchar_t* filename, void* err,
                                              void* conform, bool bForceByteSwapping, bool bWarnOfLongFilename, unsigned saveFlags, const void* platform, const void* finalTimeStamp);
typedef void*          (*CreateBlueprint_t)(void* parentClass, void* outer, const FNameRaw* newName, int bpType, void* bpClass, void* genClass, const FNameRaw* callingContext);
typedef void           (*CompileBlueprint_t)(void* bp, bool bIsRegeneratingOnLoad, bool bSkipGarbageCollection, bool bSaveIntermediateProducts, void* resultsLog);
typedef bool           (*ClassBool_t)(const void* cls);
typedef void*          (*SCS_CreateNode_t)(void* scs, void* componentClass, const FNameRaw* variableName);
typedef void           (*SCS_AddNode_t)(void* scs, void* node);
typedef void*          (*ModuleManagerGet_t)();
typedef TSharedPtrRaw* (*GetModule_t)(void* mm, TSharedPtrRaw* out, const FNameRaw* name);
typedef bool           (*IsModuleLoaded_t)(const void* mm, const FNameRaw* name);

struct EngineApi3 {
    StaticClassFn_t UObjectClass = nullptr, UClassClass = nullptr, UPackageClass = nullptr, UBlueprintClass = nullptr,
                    UBlueprintGeneratedClassClass = nullptr, UActorComponentClass = nullptr;
    FindPropertyByName_t FindPropertyByName = nullptr;
    ImportText_t ImportText = nullptr;
    ExportText_InContainer_t ExportText_InContainer = nullptr;
    PropInt_t GetOffset = nullptr, GetSize = nullptr;
    GetDefaultObject_t GetDefaultObject = nullptr;
    ObjVoid_t PostEditChange = nullptr, MarkPackageDirty = nullptr;
    GetOutermost_t GetOutermost = nullptr;
    GetOutermost_t GetOuterObj = nullptr;   // UObjectBase::GetOuter() ([this+0x28], layouts.md)
    SetFlags_t SetFlags = nullptr;
    IsChildOf_t IsChildOf = nullptr;
    IsA_t IsA = nullptr;
    StaticDuplicateObject_t StaticDuplicateObject = nullptr;
    CreatePackage_t CreatePackage = nullptr;
    EditorSavePackage_t EditorSavePackage = nullptr;
    CreateBlueprint_t CreateBlueprint = nullptr;
    CompileBlueprint_t CompileBlueprint = nullptr;
    ClassBool_t CanCreateBlueprintOfClass = nullptr;
    ObjVoid_t MarkBlueprintAsStructurallyModified = nullptr;
    SCS_CreateNode_t SCS_CreateNode = nullptr;
    SCS_AddNode_t SCS_AddNode = nullptr;
    ModuleManagerGet_t ModuleManagerGet = nullptr;
    GetModule_t GetModule = nullptr;
    IsModuleLoaded_t IsModuleLoaded = nullptr;
    bool ok = false; std::string missing;
    void Init() {
        if (ok || !missing.empty()) return;
        struct R { const wchar_t* dll; const char* sym; void** dst; };
        const R tab[] = {
            { L"UE4Editor-CoreUObject.dll", "?StaticClass@UObject@@SAPEAVUClass@@XZ", (void**)&UObjectClass },
            { L"UE4Editor-CoreUObject.dll", "?StaticClass@UClass@@SAPEAV1@XZ", (void**)&UClassClass },
            { L"UE4Editor-CoreUObject.dll", "?StaticClass@UPackage@@SAPEAVUClass@@XZ", (void**)&UPackageClass },
            { L"UE4Editor-Engine.dll",      "?StaticClass@UBlueprint@@SAPEAVUClass@@XZ", (void**)&UBlueprintClass },
            { L"UE4Editor-Engine.dll",      "?StaticClass@UBlueprintGeneratedClass@@SAPEAVUClass@@XZ", (void**)&UBlueprintGeneratedClassClass },
            { L"UE4Editor-Engine.dll",      "?StaticClass@UActorComponent@@SAPEAVUClass@@XZ", (void**)&UActorComponentClass },
            { L"UE4Editor-CoreUObject.dll", "?FindPropertyByName@UStruct@@QEBAPEAVUProperty@@VFName@@@Z", (void**)&FindPropertyByName },
            { L"UE4Editor-CoreUObject.dll", "?ImportText@UProperty@@QEBAPEB_WPEB_WPEAXHPEAVUObject@@PEAVFOutputDevice@@@Z", (void**)&ImportText },
            { L"UE4Editor-CoreUObject.dll", "?ExportText_InContainer@UProperty@@QEBA_NHAEAVFString@@PEBX1PEAVUObject@@H2@Z", (void**)&ExportText_InContainer },
            { L"UE4Editor-CoreUObject.dll", "?GetOffset_ForDebug@UProperty@@QEBAHXZ", (void**)&GetOffset },
            { L"UE4Editor-CoreUObject.dll", "?GetSize@UProperty@@QEBAHXZ", (void**)&GetSize },
            { L"UE4Editor-CoreUObject.dll", "?GetDefaultObject@UClass@@QEAAPEAVUObject@@_N@Z", (void**)&GetDefaultObject },
            { L"UE4Editor-CoreUObject.dll", "?PostEditChange@UObject@@QEAAXXZ", (void**)&PostEditChange },
            { L"UE4Editor-CoreUObject.dll", "?MarkPackageDirty@UObjectBaseUtility@@QEBAXXZ", (void**)&MarkPackageDirty },
            { L"UE4Editor-CoreUObject.dll", "?GetOutermost@UObjectBaseUtility@@QEBAPEAVUPackage@@XZ", (void**)&GetOutermost },
            { L"UE4Editor-CoreUObject.dll", "?GetOuter@UObjectBase@@QEBAPEAVUObject@@XZ", (void**)&GetOuterObj },
            { L"UE4Editor-CoreUObject.dll", "?SetFlags@UObjectBaseUtility@@QEAAXW4EObjectFlags@@@Z", (void**)&SetFlags },
            { L"UE4Editor-CoreUObject.dll", "?IsChildOf@UStruct@@QEBA_NPEBV1@@Z", (void**)&IsChildOf },
            { L"UE4Editor-CoreUObject.dll", "?IsA@UObjectBaseUtility@@QEBA_NPEBVUClass@@@Z", (void**)&IsA },
            { L"UE4Editor-CoreUObject.dll", "?StaticDuplicateObject@@YAPEAVUObject@@PEBV1@PEAV1@PEB_WW4EObjectFlags@@PEAVUClass@@W4EDuplicateForPie@@@Z", (void**)&StaticDuplicateObject },
            { L"UE4Editor-CoreUObject.dll", "?CreatePackage@@YAPEAVUPackage@@PEAVUObject@@PEB_W@Z", (void**)&CreatePackage },
            { L"UE4Editor-UnrealEd.dll",    "?SavePackage@UEditorEngine@@QEAA_NPEAVUPackage@@PEAVUObject@@W4EObjectFlags@@PEB_WPEAVFOutputDevice@@PEAVULinkerLoad@@_N6IPEBVITargetPlatform@@AEBUFDateTime@@@Z", (void**)&EditorSavePackage },
            { L"UE4Editor-UnrealEd.dll",    "?CreateBlueprint@FKismetEditorUtilities@@SAPEAVUBlueprint@@PEAVUClass@@PEAVUObject@@VFName@@W4EBlueprintType@@V?$TSubclassOf@VUBlueprint@@@@V?$TSubclassOf@VUBlueprintGeneratedClass@@@@V5@@Z", (void**)&CreateBlueprint },
            { L"UE4Editor-UnrealEd.dll",    "?CompileBlueprint@FKismetEditorUtilities@@SAXPEAVUBlueprint@@_N11PEAVFCompilerResultsLog@@@Z", (void**)&CompileBlueprint },
            { L"UE4Editor-UnrealEd.dll",    "?CanCreateBlueprintOfClass@FKismetEditorUtilities@@SA_NPEBVUClass@@@Z", (void**)&CanCreateBlueprintOfClass },
            { L"UE4Editor-UnrealEd.dll",    "?MarkBlueprintAsStructurallyModified@FBlueprintEditorUtils@@SAXPEAVUBlueprint@@@Z", (void**)&MarkBlueprintAsStructurallyModified },
            { L"UE4Editor-Engine.dll",      "?CreateNode@USimpleConstructionScript@@QEAAPEAVUSCS_Node@@PEAVUClass@@VFName@@@Z", (void**)&SCS_CreateNode },
            { L"UE4Editor-Engine.dll",      "?AddNode@USimpleConstructionScript@@QEAAXPEAVUSCS_Node@@@Z", (void**)&SCS_AddNode },
            { L"UE4Editor-Core.dll",        "?Get@FModuleManager@@SAAEAV1@XZ", (void**)&ModuleManagerGet },
            { L"UE4Editor-Core.dll",        "?GetModule@FModuleManager@@QEAA?AV?$TSharedPtr@VIModuleInterface@@$0A@@@VFName@@@Z", (void**)&GetModule },
            { L"UE4Editor-Core.dll",        "?IsModuleLoaded@FModuleManager@@QEBA_NVFName@@@Z", (void**)&IsModuleLoaded },
        };
        for (const R& r : tab) {
            *r.dst = (void*)Resolve(r.dll, r.sym);
            if (!*r.dst) { missing += std::string(r.sym) + " "; }
        }
        ok = missing.empty();
        if (!ok) FileLog("EngineApi3: exports manquants : %s", missing.c_str());
    }
};
static EngineApi3& Api3() { static EngineApi3 a; return a; }

static bool MakeFName(const wchar_t* text, FNameRaw& out)
{
    static FNameCtorW_t ctor = (FNameCtorW_t)Resolve(L"UE4Editor-Core.dll", "??0FName@@QEAA@PEB_WW4EFindName@@_N@Z");
    out = { 0, 0, 0 };
    if (!ctor) return false;
    ctor(&out, text, FNAME_Add, true);
    return true;
}

// --- Liste blanche d'ecriture (exigence Opall) : seuls les packages sous l'une des racines de la liste peuvent etre
// crees / dupliques / sauvegardes / modifies (graphs). Verifiee DANS la DLL, quel que soit l'appelant.
// 2026-09-07 : la liste est LUE AU DEMARRAGE depuis le fichier DevKitApi_whitelist.txt place A COTE DE LA DLL
// (une racine par ligne, '#' = commentaire, doit commencer par /Game/ et finit par '/'). Si le fichier manque ou ne
// contient aucune racine valide, on retombe sur kDefaultWriteRoots ci-dessous (journalise). Plus besoin de rebuild
// pour ajouter une racine : editer le fichier + relancer l'editeur.
static const char* const kDefaultWriteRoots[] = {
    "/Game/Mods/DevKitApiTest/",
    "/Game/Mods/OpallFlower/",
    "/Game/Mods/InfiniteRifle/",
};
static std::vector<std::string> g_writeRoots(std::begin(kDefaultWriteRoots), std::end(kDefaultWriteRoots));
static std::string              g_whitelistSource = "defaults (compiled in)";
static const wchar_t* kWhitelistFileName = L"DevKitApi_whitelist.txt";
// /Game/ -> dossier Content du projet. Deduit de l'emplacement de la DLL :
//   <DevKit>/Engine/Plugins/DevKitApi/Binaries/Win64/  ->  <DevKit>/Projects/ShooterGame/Content/
static const wchar_t* ContentRoot()
{
    static std::wstring root;
    if (root.empty()) {
        std::wstring d = DllDir();                       // .../Engine/Plugins/DevKitApi/Binaries/Win64/
        for (int i = 0; i < 5 && !d.empty(); ++i) {      // remonte Win64, Binaries, DevKitApi, Plugins, Engine
            size_t slash = d.find_last_of(L"\\/", d.size() - 2);
            d = (slash == std::wstring::npos) ? L"" : d.substr(0, slash + 1);
        }
        root = d.empty() ? L"G:\\DEVKIT\\ARKEditor\\Projects\\ShooterGame\\Content\\" : d + L"Projects\\ShooterGame\\Content\\";
        FileLog("content root: %ls", root.c_str());
    }
    return root.c_str();
}
static std::string WriteRootsList()
{
    std::string s;
    for (const std::string& r : g_writeRoots) { if (!s.empty()) s += " | "; s += r; }
    return s;
}

static bool IsWritablePackagePath(const std::string& pkg, std::string& why)
{
    std::string lower = pkg; for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
    size_t rootLen = 0;
    for (const std::string& r : g_writeRoots) {
        std::string root = r; for (char& ch : root) ch = (char)tolower((unsigned char)ch);
        if (lower.compare(0, root.size(), root) == 0) { rootLen = root.size(); break; }
    }
    if (rootLen == 0) { why = "package path must start with one of: " + WriteRootsList() + " [" + g_whitelistSource + "]"; return false; }
    if (pkg.size() <= rootLen) { why = "package name missing after " + pkg.substr(0, rootLen); return false; }
    if (pkg.find("..") != std::string::npos || pkg.find("//") != std::string::npos || pkg.back() == '/') { why = "invalid package path"; return false; }
    for (char ch : pkg.substr(rootLen))
        if (!(isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '/')) { why = "invalid character in package path"; return false; }
    return true;
}

// /Game/Mods/DevKitApiTest/Sub/Name -> G:\...\Content\Mods\DevKitApiTest\Sub\Name.uasset (et cree les dossiers)
static std::wstring PackageToFilename(const std::string& pkg, bool createDirs)
{
    std::wstring rel = Utf8ToWide(pkg.substr(6));   // apres "/Game/"
    for (wchar_t& ch : rel) if (ch == L'/') ch = L'\\';
    std::wstring full = std::wstring(ContentRoot()) + rel + L".uasset";
    if (createDirs) {
        size_t pos = wcslen(ContentRoot());
        while ((pos = full.find(L'\\', pos)) != std::wstring::npos) { CreateDirectoryW(full.substr(0, pos).c_str(), nullptr); ++pos; }
    }
    return full;
}

static std::string PathNameOf(const void* obj)
{
    FStringRaw tmp{}; Api().GetPathName(obj, &tmp, nullptr); return TakeFString(tmp);
}
static std::string NameOf(const void* obj)
{
    FStringRaw tmp{}; Api().GetName(obj, &tmp); return TakeFString(tmp);
}

// Objet par chemin ; charge depuis le disque si absent et bLoad
static void* ResolveObject(const std::string& path, bool bLoad)
{
    std::wstring w = Utf8ToWide(path);
    void* obj = Api().StaticFindObject(nullptr, ANY_PACKAGE, w.c_str(), false);
    if (!obj && bLoad && Api().UObjectClass) obj = Api().StaticLoadObject(Api().UObjectClass, nullptr, w.c_str(), nullptr, 0, nullptr, true);
    return obj;
}

// Propriete UPROPERTY d'un objet via la reflexion (jamais d'offset code en dur)
struct PropInfo { void* prop = nullptr; int offset = 0; int size = 0; int arrayDim = 1; int elemSize = 0; std::string type; };
static bool FindProp(void* obj, const std::string& name, PropInfo& out)
{
    EngineApi3& a = Api3();
    void* cls = Api().GetClass(obj);
    FNameRaw fn; if (!cls || !MakeFName(Utf8ToWide(name).c_str(), fn)) return false;
    out.prop = a.FindPropertyByName(cls, &fn);
    if (!out.prop) return false;
    out.offset = a.GetOffset(out.prop);
    out.size = a.GetSize(out.prop);
    out.arrayDim = *(int32_t*)((uint8_t*)out.prop + 0x40);   // UProperty::ArrayDim (layouts.md, prouve par GetSize)
    out.elemSize = out.arrayDim > 0 ? out.size / out.arrayDim : out.size;
    if (void* pc = Api().GetClass(out.prop)) out.type = NameOf(pc);
    return true;
}
static void* ReadObjectProp(void* obj, const char* name)   // lit un UPROPERTY() UObject* (GeneratedClass, SimpleConstructionScript)
{
    PropInfo p; if (!FindProp(obj, name, p) || p.size != 8) return nullptr;
    return *(void**)((uint8_t*)obj + p.offset);
}

// Cible d'edition : UClass -> CDO ; UBlueprint -> CDO de GeneratedClass ; sinon l'objet lui-meme
static void* EditTarget(void* obj, bool wantCdo, std::string& note)
{
    EngineApi3& a = Api3();
    if (!wantCdo) return obj;
    if (a.IsA(obj, a.UClassClass())) { note = "cdo of class"; return a.GetDefaultObject(obj, true); }
    if (a.IsA(obj, a.UBlueprintClass())) {
        void* gen = ReadObjectProp(obj, "GeneratedClass");
        if (!gen) { note = "blueprint without GeneratedClass"; return nullptr; }
        note = "cdo of blueprint generated class"; return a.GetDefaultObject(gen, true);
    }
    note = "object itself"; return obj;
}

// Notification Asset Registry : FModuleManager::GetModule("AssetRegistry") -> FAssetRegistryModule::Get() (slot 8, +0x40 :
// 3 appelants lus dans AssetTools.dll) -> IAssetRegistry::AssetCreated (INDEX 26, +0xD0 : vtable FAssetRegistry a
// AssetRegistry.dll RVA 0x55D80, fonction 0x22D80 = ensure("NewAsset") + IsAsset + GetOutermost + LongPackageNameToFilename).
static bool NotifyAssetCreated(void* obj, std::string& err)
{
    EngineApi3& a = Api3();
    FNameRaw n; MakeFName(L"AssetRegistry", n);
    void* mm = a.ModuleManagerGet();
    if (!mm || !a.IsModuleLoaded(mm, &n)) { err = "AssetRegistry module not loaded"; return false; }
    TSharedPtrRaw sp{ nullptr, nullptr };
    a.GetModule(mm, &sp, &n);
    if (!sp.Object) { err = "GetModule(AssetRegistry) returned null"; return false; }
    void** modVt = *(void***)sp.Object;
    typedef void* (*ModuleGet_t)(void*);
    void* registry = ((ModuleGet_t)modVt[8])(sp.Object);
    if (sp.Controller) --sp.Controller[0];   // relache la copie du TSharedPtr (compteur non thread-safe, +0)
    if (!registry) { err = "FAssetRegistryModule::Get() returned null"; return false; }
    void** regVt = *(void***)registry;
    typedef void (*AssetCreated_t)(void*, void*);
    ((AssetCreated_t)regVt[26])(registry, obj);
    return true;
}

// ---------------------------------------------------------------------------------------------
// Mini JSON (objets plats : chaines / nombres / bools) — suffisant pour le protocole v0
// ---------------------------------------------------------------------------------------------
struct JsonValue { std::string str; double num = 0; bool isNum = false, isBool = false, b = false; };
typedef std::vector<std::pair<std::string, JsonValue>> JsonObject;

static const JsonValue* JsonGet(const JsonObject& o, const char* key)
{
    for (auto& kv : o) if (kv.first == key) return &kv.second;
    return nullptr;
}

static bool ParseString(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\') {
            if (i >= s.size()) return false;
            char e = s[i++];
            switch (e) {
            case '"': out += '"'; break;  case '\\': out += '\\'; break; case '/': out += '/'; break;
            case 'b': out += '\b'; break; case 'f': out += '\f'; break;  case 'n': out += '\n'; break;
            case 'r': out += '\r'; break; case 't': out += '\t'; break;
            case 'u': {
                if (i + 4 > s.size()) return false;
                unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16); i += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 6 <= s.size() && s[i] == '\\' && s[i + 1] == 'u') {
                    unsigned lo = (unsigned)strtoul(s.substr(i + 2, 4).c_str(), nullptr, 16);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) { cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); i += 6; }
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
                else if (cp < 0x10000) { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                else { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
                break; }
            default: return false;
            }
        } else out += c;
    }
    return false;
}

static void SkipWs(const std::string& s, size_t& i) { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) ++i; }

static bool ParseObject(const std::string& s, JsonObject& out)
{
    size_t i = 0; SkipWs(s, i);
    if (i >= s.size() || s[i] != '{') return false;
    ++i; SkipWs(s, i);
    if (i < s.size() && s[i] == '}') return true;
    while (i < s.size()) {
        SkipWs(s, i);
        std::string key; if (!ParseString(s, i, key)) return false;
        SkipWs(s, i); if (i >= s.size() || s[i] != ':') return false; ++i; SkipWs(s, i);
        JsonValue v;
        if (i >= s.size()) return false;
        if (s[i] == '"') { if (!ParseString(s, i, v.str)) return false; }
        else if (s.compare(i, 4, "true") == 0) { v.isBool = true; v.b = true; i += 4; }
        else if (s.compare(i, 5, "false") == 0) { v.isBool = true; v.b = false; i += 5; }
        else if (s.compare(i, 4, "null") == 0) { i += 4; }
        else {
            const char* start = s.c_str() + i; char* end = nullptr;
            v.num = strtod(start, &end); if (end == start) return false;
            v.isNum = true; i += (size_t)(end - start);
        }
        out.emplace_back(key, v);
        SkipWs(s, i);
        if (i < s.size() && s[i] == ',') { ++i; continue; }
        if (i < s.size() && s[i] == '}') return true;
        return false;
    }
    return false;
}

static std::string JsonEscape(const std::string& s)
{
    std::string o; o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
        case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;  case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default:
            if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; } else o += (char)c;
        }
    }
    return o;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// ---------------------------------------------------------------------------------------------
// File de commandes -> thread principal (hook WH_GETMESSAGE + PostThreadMessage)
// ---------------------------------------------------------------------------------------------
struct Command {
    std::string idJson;          // id renvoye tel quel ("1", "\"abc\"", ou "null")
    std::string cmd;
    JsonObject  args;
    std::string reply;           // rempli par le thread principal
    HANDLE      done = nullptr;  // event signale quand reply est prete
};

// g_hInstance : defini en tete de fichier (section journal)
static DWORD                g_mainThreadId = 0;
static HHOOK                g_hook = nullptr;
static std::mutex           g_queueMutex;
static std::deque<Command*> g_queue;
static std::atomic<bool>    g_running{ false };
static std::atomic<bool>    g_draining{ false };

// Charge DevKitApi_whitelist.txt (a cote de la DLL). Appele une fois dans StartupModule, AVANT le thread socket.
static void LoadWhitelistFile()
{
    wchar_t dll[MAX_PATH] = {};
    if (!g_hInstance || GetModuleFileNameW(g_hInstance, dll, MAX_PATH) == 0) { FileLog("whitelist: GetModuleFileNameW failed -> defaults"); return; }
    std::wstring path = dll;
    size_t slash = path.find_last_of(L"\\/");
    path = (slash == std::wstring::npos ? L"" : path.substr(0, slash + 1)) + kWhitelistFileName;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        FileLog("whitelist: %ls ABSENT -> defaults (%s)", path.c_str(), WriteRootsList().c_str());
        return;
    }
    std::vector<std::string> roots;
    char line[1024]; int lineNo = 0;
    while (fgets(line, sizeof(line), f)) {
        ++lineNo;
        std::string t = line;
        if (lineNo == 1 && t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t = t.substr(3); // BOM UTF-8
        size_t hash = t.find('#'); if (hash != std::string::npos) t = t.substr(0, hash);
        size_t a = t.find_first_not_of(" \t\r\n"), b = t.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) continue;
        t = t.substr(a, b - a + 1);
        for (char& ch : t) if (ch == '\\') ch = '/';
        if (t.back() != '/') t += '/';
        bool valid = t.compare(0, 6, "/Game/") == 0 && t.size() > 6 && t.find("..") == std::string::npos && t.find("//") == std::string::npos;
        for (char ch : t) if (valid && !(isalnum((unsigned char)ch) || ch == '_' || ch == '-' || ch == '/')) valid = false;
        if (!valid) { FileLog("whitelist: line %d IGNORED (must be /Game/.../ ) : '%s'", lineNo, t.c_str()); continue; }
        bool dup = false; for (const std::string& r : roots) if (_stricmp(r.c_str(), t.c_str()) == 0) dup = true;
        if (!dup) roots.push_back(t);
    }
    fclose(f);
    if (roots.empty()) { FileLog("whitelist: %ls sans racine valide -> defaults (%s)", path.c_str(), WriteRootsList().c_str()); return; }
    g_writeRoots = roots;
    char src[MAX_PATH * 2]; _snprintf_s(src, sizeof(src), _TRUNCATE, "file %ls", path.c_str());
    g_whitelistSource = src;
    FileLog("whitelist: %d racine(s) depuis %ls : %s", (int)roots.size(), path.c_str(), WriteRootsList().c_str());
}
static SOCKET               g_listenSock = INVALID_SOCKET;
static std::thread          g_socketThread;
static const UINT           WM_DEVKITAPI_WAKE = WM_USER + 1;
static const unsigned short kPort = 9877;

static std::string MakeReply(const std::string& idJson, bool ok, const std::string& payloadJson /* deja formate, sans accolades */)
{
    std::string r = "{\"id\":" + idJson + ",\"ok\":" + (ok ? "true" : "false");
    if (!payloadJson.empty()) r += "," + payloadJson;
    r += "}\n";
    return r;
}

static const JsonValue* ArgStr(const JsonObject& o, const char* key) { const JsonValue* v = JsonGet(o, key); return (v && !v->str.empty()) ? v : nullptr; }
static bool ArgBool(const JsonObject& o, const char* key, bool def) { const JsonValue* v = JsonGet(o, key); return (v && v->isBool) ? v->b : def; }
static int  ArgInt(const JsonObject& o, const char* key, int def) { const JsonValue* v = JsonGet(o, key); return (v && v->isNum) ? (int)v->num : def; }
static std::string ErrReply(const Command* c, const std::string& msg) { return MakeReply(c->idJson, false, "\"error\":" + JsonStr(msg)); }

// Phase 4 (ecriture de graphs) : add_variable / remove_variable / add_function / remove_graph / paste_t3d / compile_blueprint
#include "DevKitApi_Phase4.h"

// Execution d'une commande SUR LE THREAD PRINCIPAL. Aucun objet C++ a destructeur dans la fonction
// SEH (contrainte /EHsc + __try) -> le corps C++ est dans ExecuteCommandBody, le __try dans un wrapper.
static void ExecuteCommandBody(Command* c)
{
    if (Phase4Command(c)) return;
    if (c->cmd == "log") {
        const JsonValue* t = JsonGet(c->args, "text");
        std::string text = t ? t->str : "";
        std::wstring w = L"[DevKitApi] " + Utf8ToWide(text);
        EngineLog(ELogVerbosity_Log, w.c_str());
        FileLog("log: %s", text.c_str());
        c->reply = MakeReply(c->idJson, true, "");
    }
    else if (c->cmd == "editor_ready") {
        c->reply = MakeReply(c->idJson, true, std::string("\"result\":") + (EditorReady() ? "true" : "false"));
    }
    else if (c->cmd == "exec") {
        // {"cmd":"exec","command":"DumpConsoleCommands","to_log":true,"world":true}
        Api().Init();
        const JsonValue* cv = JsonGet(c->args, "command");
        if (!cv || cv->str.empty()) { c->reply = MakeReply(c->idJson, false, "\"error\":\"missing command\""); return; }
        if (!Api().EditorExec) { c->reply = MakeReply(c->idJson, false, "\"error\":\"UUnrealEdEngine::Exec not resolved\""); return; }
        void* editor = GEditorPtr();
        if (!editor || !EditorReady()) { c->reply = MakeReply(c->idJson, false, "\"error\":\"editor not ready\""); return; }
        const JsonValue* tl = JsonGet(c->args, "to_log");   bool toLog = tl && tl->isBool ? tl->b : true;
        const JsonValue* uw = JsonGet(c->args, "world");    bool useWorld = uw && uw->isBool ? uw->b : true;
        // phase 4 : "world_path" = chemin d'un UWorld precis (ex. le monde PIE '/Game/Maps/UEDPIE_0_X.X') pour les commandes
        // qui agissent sur un monde (ke * Event, summon...) — GWorld reste le monde editeur pendant un PIE.
        void* worldObj = nullptr;
        if (const JsonValue* wp = ArgStr(c->args, "world_path")) {
            worldObj = ResolveObject(wp->str, false);
            if (!worldObj) { c->reply = ErrReply(c, "world_path not found (not loaded): " + wp->str); return; }
        }
        std::string out; out.reserve(4096);
        FCaptureDevice dev{ g_captureVtable, false, true, &out, 0, toLog };
        std::wstring wcmd = Utf8ToWide(cv->str);
        FileLog("exec: %s", cv->str.c_str());
        // ⚠️ Les corps exportes de UUnrealEdEngine::Exec / UEditorEngine::Exec / UEngine::Exec attendent `this` =
        // sous-objet FExec (= objet + 0x38) et font `lea rcx,[this-0x38]` pour retrouver l'UEngine (capstone,
        // UnrealEd RVA 0x2858B8 / 0x575A58, Engine 0xA0BBBA). Passer GEditor brut -> AV (constate 2026-09-03).
        static const size_t kExecThisAdjust = 0x38;
        void* execThis = (void*)((uint8_t*)editor + kExecThisAdjust);
        bool handled = Api().EditorExec(execThis, worldObj ? worldObj : (useWorld ? GWorldPtr() : nullptr), wcmd.c_str(), &dev);
        bool truncated = false;
        if (out.size() > 65536) { out.resize(65536); truncated = true; }
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"handled\":" + std::string(handled ? "true" : "false")
            + ",\"lines\":" + std::to_string(dev.lines) + ",\"truncated\":" + (truncated ? "true" : "false")
            + ",\"output\":" + JsonStr(out) + "}");
    }
    else if (c->cmd == "find_object") {
        // {"cmd":"find_object","path":"/Script/Engine.Actor","class":"/Script/CoreUObject.Class"(opt)}
        Api().Init();
        const JsonValue* pv = JsonGet(c->args, "path");
        if (!pv || pv->str.empty()) { c->reply = MakeReply(c->idJson, false, "\"error\":\"missing path\""); return; }
        if (!Api().ok) { c->reply = MakeReply(c->idJson, false, "\"error\":\"CoreUObject exports not resolved\""); return; }
        void* cls = nullptr;
        if (const JsonValue* kv = JsonGet(c->args, "class")) {
            if (!kv->str.empty()) {
                cls = Api().StaticFindObject(nullptr, ANY_PACKAGE, Utf8ToWide(kv->str).c_str(), false);
                if (!cls) { c->reply = MakeReply(c->idJson, false, "\"error\":\"class not found: " + JsonEscape(kv->str) + "\""); return; }
            }
        }
        void* obj = Api().StaticFindObject(cls, ANY_PACKAGE, Utf8ToWide(pv->str).c_str(), false);
        FileLog("find_object: %s -> %p", pv->str.c_str(), obj);
        c->reply = MakeReply(c->idJson, true, "\"result\":" + ObjectInfoJson(obj));
    }
    else if (c->cmd == "load_asset") {
        // {"cmd":"load_asset","path":"/Game/Mods/X/Y.Y"}  -> StaticLoadObject(UObject::StaticClass(), null, path, null, LOAD_None, null, true)
        Api().Init();
        const JsonValue* pv = JsonGet(c->args, "path");
        if (!pv || pv->str.empty()) { c->reply = MakeReply(c->idJson, false, "\"error\":\"missing path\""); return; }
        if (!Api().ok || !Api().UObjectClass) { c->reply = MakeReply(c->idJson, false, "\"error\":\"CoreUObject exports or UObject class not resolved\""); return; }
        if (!EditorReady()) { c->reply = MakeReply(c->idJson, false, "\"error\":\"editor not ready\""); return; }
        DWORD t0 = GetTickCount();
        void* obj = Api().StaticLoadObject(Api().UObjectClass, nullptr, Utf8ToWide(pv->str).c_str(), nullptr, 0 /*LOAD_None*/, nullptr, true);
        FileLog("load_asset: %s -> %p (%lu ms)", pv->str.c_str(), obj, GetTickCount() - t0);
        c->reply = MakeReply(c->idJson, true, "\"result\":" + ObjectInfoJson(obj) + ",\"ms\":" + std::to_string(GetTickCount() - t0));
    }
    // ------------------------------------------------------------------ phase 3
    else if (c->cmd == "get_property" || c->cmd == "set_property") {
        // {"cmd":"get_property","object":"/Game/...X.X","property":"Name","index":0,"cdo":true}
        // {"cmd":"set_property","object":...,"property":...,"value":"texte ImportText","index":0,"cdo":true}
        Api().Init(); Api3().Init();
        if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return; }
        if (!EditorReady()) { c->reply = ErrReply(c, "editor not ready"); return; }
        const JsonValue* ov = ArgStr(c->args, "object"); const JsonValue* pv = ArgStr(c->args, "property");
        if (!ov || !pv) { c->reply = ErrReply(c, "missing object or property"); return; }
        void* obj = ResolveObject(ov->str, true);
        if (!obj) { c->reply = ErrReply(c, "object not found: " + ov->str); return; }
        std::string note; void* target = EditTarget(obj, ArgBool(c->args, "cdo", true), note);
        if (!target) { c->reply = ErrReply(c, note); return; }
        PropInfo p;
        if (!FindProp(target, pv->str, p)) { c->reply = ErrReply(c, "property not found on " + PathNameOf(target) + ": " + pv->str); return; }
        int index = ArgInt(c->args, "index", 0);
        if (index < 0 || index >= p.arrayDim) { c->reply = ErrReply(c, "index out of range (ArrayDim=" + std::to_string(p.arrayDim) + ")"); return; }
        std::string errText; FCaptureDevice errDev{ g_captureVtable, false, true, &errText, 0, false };
        bool imported = true;
        if (c->cmd == "set_property") {
            const JsonValue* vv = JsonGet(c->args, "value");
            if (!vv) { c->reply = ErrReply(c, "missing value"); return; }
            std::string valueStr = vv->isNum ? std::to_string(vv->num) : vv->isBool ? (vv->b ? "True" : "False") : vv->str;
            std::wstring wval = Utf8ToWide(valueStr);
            void* data = (uint8_t*)target + p.offset + index * p.elemSize;
            const wchar_t* r = Api3().ImportText(p.prop, wval.c_str(), data, 0 /*PPF_None*/, target, &errDev);
            imported = (r != nullptr);
            FileLog("set_property: %s.%s[%d] = %s -> %s", PathNameOf(target).c_str(), pv->str.c_str(), index, valueStr.c_str(), imported ? "ok" : "FAILED");
            if (imported) { Api3().PostEditChange(target); Api3().MarkPackageDirty(target); }
        }
        FStringRaw out{};
        Api3().ExportText_InContainer(p.prop, index, &out, target, nullptr, target, 0 /*PPF_None*/, nullptr);
        std::string value = TakeFString(out);
        c->reply = MakeReply(c->idJson, imported, "\"result\":{\"object\":" + JsonStr(PathNameOf(target)) + ",\"target\":" + JsonStr(note)
            + ",\"property\":" + JsonStr(pv->str) + ",\"type\":" + JsonStr(p.type) + ",\"array_dim\":" + std::to_string(p.arrayDim)
            + ",\"offset\":" + std::to_string(p.offset) + ",\"size\":" + std::to_string(p.size) + ",\"index\":" + std::to_string(index)
            + ",\"value\":" + JsonStr(value) + (imported ? "" : ",\"error\":\"ImportText failed\"") + ",\"messages\":" + JsonStr(errText) + "}");
    }
    else if (c->cmd == "save_package") {
        // {"cmd":"save_package","package":"/Game/Mods/DevKitApiTest/X"}  ou  {"asset":"/Game/Mods/DevKitApiTest/X.X"}
        Api().Init(); Api3().Init();
        if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return; }
        if (!EditorReady() || !GEditorPtr()) { c->reply = ErrReply(c, "editor not ready"); return; }
        void* pkg = nullptr;
        if (const JsonValue* av = ArgStr(c->args, "asset")) {
            void* obj = ResolveObject(av->str, false);
            if (!obj) { c->reply = ErrReply(c, "asset not found (not loaded): " + av->str); return; }
            pkg = Api3().GetOutermost(obj);
        } else if (const JsonValue* kv = ArgStr(c->args, "package")) {
            pkg = Api().StaticFindObject(Api3().UPackageClass(), nullptr, Utf8ToWide(kv->str).c_str(), false);
            if (!pkg) { c->reply = ErrReply(c, "package not found (not loaded): " + kv->str); return; }
        } else { c->reply = ErrReply(c, "missing package or asset"); return; }
        std::string pkgPath = PathNameOf(pkg), why;
        if (!IsWritablePackagePath(pkgPath, why)) { c->reply = ErrReply(c, "refused (whitelist): " + pkgPath + " : " + why); return; }
        std::wstring file = PackageToFilename(pkgPath, true);
        std::string log; FCaptureDevice dev{ g_captureVtable, false, true, &log, 0, true };
        int64_t noTimeStamp = 0;   // FDateTime::MinValue() = 0 ticks
        FileLog("save_package: %s -> %S", pkgPath.c_str(), file.c_str());
        // Base=null + RF_Standalone (0x2) = sauve tous les objets standalone du package ; SAVE_None ; pas de dialogue nom long
        bool saved = Api3().EditorSavePackage(GEditorPtr(), pkg, nullptr, 0x2, file.c_str(), &dev, nullptr, false, false, 0, nullptr, &noTimeStamp);
        WIN32_FILE_ATTRIBUTE_DATA fad{}; long long fsize = -1;
        if (GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &fad)) fsize = ((long long)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        c->reply = MakeReply(c->idJson, saved, "\"result\":{\"saved\":" + std::string(saved ? "true" : "false") + ",\"package\":" + JsonStr(pkgPath)
            + ",\"file\":" + JsonStr(WideToUtf8(file.c_str(), (int)file.size())) + ",\"file_size\":" + std::to_string(fsize) + ",\"messages\":" + JsonStr(log) + "}");
    }
    else if (c->cmd == "duplicate_asset") {
        // {"cmd":"duplicate_asset","source":"/Game/.../X.X","dest_package":"/Game/Mods/DevKitApiTest/Y","dest_name":"Y"(opt)}
        Api().Init(); Api3().Init();
        if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return; }
        if (!EditorReady()) { c->reply = ErrReply(c, "editor not ready"); return; }
        const JsonValue* sv = ArgStr(c->args, "source"); const JsonValue* dv = ArgStr(c->args, "dest_package");
        if (!sv || !dv) { c->reply = ErrReply(c, "missing source or dest_package"); return; }
        std::string why; if (!IsWritablePackagePath(dv->str, why)) { c->reply = ErrReply(c, "refused (whitelist): " + dv->str + " : " + why); return; }
        std::string name = ArgStr(c->args, "dest_name") ? ArgStr(c->args, "dest_name")->str : dv->str.substr(dv->str.rfind('/') + 1);
        void* src = ResolveObject(sv->str, true);
        if (!src) { c->reply = ErrReply(c, "source not found: " + sv->str); return; }
        std::wstring wpkg = Utf8ToWide(dv->str), wname = Utf8ToWide(name);
        if (Api().StaticFindObject(nullptr, ANY_PACKAGE, (wpkg + L"." + wname).c_str(), false)) { c->reply = ErrReply(c, "destination already exists: " + dv->str + "." + name); return; }
        void* pkg = Api3().CreatePackage(nullptr, wpkg.c_str());
        if (!pkg) { c->reply = ErrReply(c, "CreatePackage failed"); return; }
        DWORD t0 = GetTickCount();
        void* dup = Api3().StaticDuplicateObject(src, pkg, wname.c_str(), 0xFFFFFFFFu /*RF_AllFlags*/, nullptr, 0 /*SDO_No_DuplicateForPie*/);
        if (!dup) { c->reply = ErrReply(c, "StaticDuplicateObject returned null"); return; }
        Api3().SetFlags(dup, 0x1 | 0x2);   // RF_Public | RF_Standalone
        std::string nerr; bool notified = NotifyAssetCreated(dup, nerr);
        Api3().MarkPackageDirty(dup);
        FileLog("duplicate_asset: %s -> %s.%s (%p, %lu ms, registry=%d %s)", sv->str.c_str(), dv->str.c_str(), name.c_str(), dup, GetTickCount() - t0, notified, nerr.c_str());
        std::wstring dfile = PackageToFilename(dv->str, false);
        c->reply = MakeReply(c->idJson, true, "\"result\":" + ObjectInfoJson(dup) + ",\"package\":" + JsonStr(PathNameOf(pkg))
            + ",\"file\":" + JsonStr(WideToUtf8(dfile.c_str(), (int)dfile.size()))
            + ",\"asset_registry_notified\":" + (notified ? "true" : "false") + (notified ? "" : ",\"registry_error\":" + JsonStr(nerr)) + ",\"ms\":" + std::to_string(GetTickCount() - t0));
    }
    else if (c->cmd == "create_blueprint") {
        // {"cmd":"create_blueprint","parent_class":"/Script/Engine.Actor","package":"/Game/Mods/DevKitApiTest/BP_X","name":"BP_X"(opt)}
        Api().Init(); Api3().Init();
        if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return; }
        if (!EditorReady()) { c->reply = ErrReply(c, "editor not ready"); return; }
        const JsonValue* pcv = ArgStr(c->args, "parent_class"); const JsonValue* kv = ArgStr(c->args, "package");
        if (!pcv || !kv) { c->reply = ErrReply(c, "missing parent_class or package"); return; }
        std::string why; if (!IsWritablePackagePath(kv->str, why)) { c->reply = ErrReply(c, "refused (whitelist): " + kv->str + " : " + why); return; }
        std::string name = ArgStr(c->args, "name") ? ArgStr(c->args, "name")->str : kv->str.substr(kv->str.rfind('/') + 1);
        void* parent = ResolveObject(pcv->str, true);
        if (!parent) { c->reply = ErrReply(c, "parent class not found: " + pcv->str); return; }
        if (!Api3().IsA(parent, Api3().UClassClass())) { c->reply = ErrReply(c, "parent_class is not a UClass: " + PathNameOf(parent)); return; }
        if (!Api3().CanCreateBlueprintOfClass(parent)) { c->reply = ErrReply(c, "CanCreateBlueprintOfClass refused: " + PathNameOf(parent)); return; }
        std::wstring wpkg = Utf8ToWide(kv->str), wname = Utf8ToWide(name);
        if (Api().StaticFindObject(nullptr, ANY_PACKAGE, (wpkg + L"." + wname).c_str(), false)) { c->reply = ErrReply(c, "blueprint already exists: " + kv->str + "." + name); return; }
        void* pkg = Api3().CreatePackage(nullptr, wpkg.c_str());
        if (!pkg) { c->reply = ErrReply(c, "CreatePackage failed"); return; }
        FNameRaw fn, none{ 0, 0, 0 }; MakeFName(wname.c_str(), fn);
        DWORD t0 = GetTickCount();
        void* bp = Api3().CreateBlueprint(parent, pkg, &fn, 0 /*BPTYPE_Normal*/, Api3().UBlueprintClass(), Api3().UBlueprintGeneratedClassClass(), &none);
        if (!bp) { c->reply = ErrReply(c, "CreateBlueprint returned null"); return; }
        std::string nerr; bool notified = NotifyAssetCreated(bp, nerr);
        Api3().MarkPackageDirty(bp);
        void* gen = ReadObjectProp(bp, "GeneratedClass");
        FileLog("create_blueprint: %s parent %s -> %p (gen %p, %lu ms, registry=%d %s)", kv->str.c_str(), pcv->str.c_str(), bp, gen, GetTickCount() - t0, notified, nerr.c_str());
        c->reply = MakeReply(c->idJson, true, "\"result\":" + ObjectInfoJson(bp) + ",\"generated_class\":" + JsonStr(gen ? PathNameOf(gen) : "")
            + ",\"file\":" + JsonStr(WideToUtf8(PackageToFilename(kv->str, false).c_str(), (int)PackageToFilename(kv->str, false).size()))
            + ",\"asset_registry_notified\":" + (notified ? "true" : "false") + (notified ? "" : ",\"registry_error\":" + JsonStr(nerr)) + ",\"ms\":" + std::to_string(GetTickCount() - t0));
    }
    // compile_blueprint : depuis la phase 4, traite dans DevKitApi_Phase4.h (Phase4Command, avec capture des messages)
    else if (c->cmd == "add_component") {
        // {"cmd":"add_component","blueprint":"/Game/Mods/DevKitApiTest/BP_X.BP_X","component_class":"/Script/Engine.StaticMeshComponent","name":"Mesh"}
        Api().Init(); Api3().Init();
        if (!Api().ok || !Api3().ok) { c->reply = ErrReply(c, "exports not resolved: " + Api3().missing); return; }
        if (!EditorReady()) { c->reply = ErrReply(c, "editor not ready"); return; }
        const JsonValue* bv = ArgStr(c->args, "blueprint"); const JsonValue* cv = ArgStr(c->args, "component_class"); const JsonValue* nv = ArgStr(c->args, "name");
        if (!bv || !cv || !nv) { c->reply = ErrReply(c, "missing blueprint, component_class or name"); return; }
        void* bp = ResolveObject(bv->str, true);
        if (!bp || !Api3().IsA(bp, Api3().UBlueprintClass())) { c->reply = ErrReply(c, "blueprint not found or not a UBlueprint: " + bv->str); return; }
        void* compClass = ResolveObject(cv->str, true);
        if (!compClass || !Api3().IsA(compClass, Api3().UClassClass())) { c->reply = ErrReply(c, "component_class not found or not a UClass: " + cv->str); return; }
        if (!Api3().IsChildOf(compClass, Api3().UActorComponentClass())) { c->reply = ErrReply(c, "component_class is not an ActorComponent: " + cv->str); return; }
        void* scs = ReadObjectProp(bp, "SimpleConstructionScript");
        if (!scs) { c->reply = ErrReply(c, "blueprint has no SimpleConstructionScript (not actor-based?)"); return; }
        FNameRaw fn; MakeFName(Utf8ToWide(nv->str).c_str(), fn);
        void* node = Api3().SCS_CreateNode(scs, compClass, &fn);
        if (!node) { c->reply = ErrReply(c, "USimpleConstructionScript::CreateNode returned null"); return; }
        Api3().SCS_AddNode(scs, node);
        Api3().MarkBlueprintAsStructurallyModified(bp);
        Api3().MarkPackageDirty(bp);
        void* tmpl = ReadObjectProp(node, "ComponentTemplate");
        FileLog("add_component: %s += %s '%s' -> node %p template %p", bv->str.c_str(), cv->str.c_str(), nv->str.c_str(), node, tmpl);
        c->reply = MakeReply(c->idJson, true, "\"result\":{\"blueprint\":" + JsonStr(PathNameOf(bp)) + ",\"node\":" + ObjectInfoJson(node)
            + ",\"component_template\":" + ObjectInfoJson(tmpl) + "}");
    }
    else {
        c->reply = MakeReply(c->idJson, false, "\"error\":\"unknown command\"");
    }
}

static int SehFilter(unsigned code, EXCEPTION_POINTERS* ep, Command* c)
{
    FileLog("EXCEPTION 0x%08X dans la commande '%s' (addr %p)", code, c->cmd.c_str(),
            ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr);
    return EXCEPTION_EXECUTE_HANDLER;
}

static void SetExceptionReply(Command* c)
{
    c->reply = MakeReply(c->idJson, false, "\"error\":\"exception during command\"");
}

// Pas d'objet C++ a destructeur ici (C2712) : tout est delegue.
static void ExecuteCommandSEH(Command* c)
{
    __try {
        ExecuteCommandBody(c);
    }
    __except (SehFilter(GetExceptionCode(), GetExceptionInformation(), c)) {
        SetExceptionReply(c);
    }
}

static void DrainQueue()
{
    if (g_draining.exchange(true)) return;   // anti-reentrance (une commande peut pomper des messages)
    for (;;) {
        Command* c = nullptr;
        { std::lock_guard<std::mutex> lk(g_queueMutex); if (!g_queue.empty()) { c = g_queue.front(); g_queue.pop_front(); } }
        if (!c) break;
        ExecuteCommandSEH(c);
        SetEvent(c->done);
    }
    g_draining = false;
}

static LRESULT CALLBACK HookProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code >= 0 && g_running) DrainQueue();
    return CallNextHookEx(g_hook, code, wParam, lParam);
}

// Thread socket : poste la commande et attend la reponse (timeout -> erreur, la commande reste en file
// et sera quand meme executee ; on ne libere alors pas l'objet tout de suite).
static std::string RunOnMainThread(const std::string& idJson, const std::string& cmd, const JsonObject& args)
{
    // Delai d'attente : "timeout_ms" dans la requete, sinon 600 s pour load_asset (un blueprint de dino
    // charge des dizaines de dependances : >30 s constate sur Dodo_Character_BP), 30 s pour le reste.
    DWORD timeoutMs = (cmd == "load_asset") ? 600000 : 30000;
    if (const JsonValue* t = JsonGet(args, "timeout_ms")) if (t->isNum && t->num > 0) timeoutMs = (DWORD)t->num;
    Command* c = new Command();
    c->idJson = idJson; c->cmd = cmd; c->args = args;
    c->done = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    { std::lock_guard<std::mutex> lk(g_queueMutex); g_queue.push_back(c); }
    PostThreadMessageW(g_mainThreadId, WM_DEVKITAPI_WAKE, 0, 0);
    DWORD w = WaitForSingleObject(c->done, timeoutMs);
    std::string reply;
    if (w == WAIT_OBJECT_0) {
        reply = c->reply; CloseHandle(c->done); delete c;
    } else {
        // Toujours en file (thread principal bloque ?) : on la retire si possible, sinon fuite volontaire.
        bool removed = false;
        { std::lock_guard<std::mutex> lk(g_queueMutex);
          for (auto it = g_queue.begin(); it != g_queue.end(); ++it) if (*it == c) { g_queue.erase(it); removed = true; break; } }
        if (removed) { CloseHandle(c->done); delete c; }
        reply = MakeReply(idJson, false, "\"error\":\"timeout: main thread did not process the command (editor busy?)\"");
    }
    return reply;
}

static std::string HandleLine(const std::string& line)
{
    JsonObject o;
    if (!ParseObject(line, o)) return MakeReply("null", false, "\"error\":\"invalid json\"");
    std::string idJson = "null";
    if (const JsonValue* id = JsonGet(o, "id")) {
        if (id->isNum) { char b[64]; sprintf_s(b, "%.17g", id->num); idJson = b; }
        else if (!id->str.empty()) idJson = "\"" + JsonEscape(id->str) + "\"";
    }
    const JsonValue* cmdv = JsonGet(o, "cmd");
    if (!cmdv || cmdv->str.empty()) return MakeReply(idJson, false, "\"error\":\"missing cmd\"");
    const std::string& cmd = cmdv->str;

    if (cmd == "ping") {   // repondu inline : ne lit que des globals (pas d'UObject)
        return MakeReply(idJson, true, std::string("\"result\":\"pong\",\"editor_ready\":") + (EditorReady() ? "true" : "false"));
    }
    return RunOnMainThread(idJson, cmd, o);
}

static void SocketThreadMain()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { FileLog("WSAStartup failed"); return; }
    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) { FileLog("socket() failed %d", WSAGetLastError()); WSACleanup(); return; }
    BOOL reuse = TRUE; setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(kPort);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(ls, (sockaddr*)&addr, sizeof(addr)) != 0) { FileLog("bind(127.0.0.1:%u) failed %d", kPort, WSAGetLastError()); closesocket(ls); WSACleanup(); return; }
    if (listen(ls, 4) != 0) { FileLog("listen failed %d", WSAGetLastError()); closesocket(ls); WSACleanup(); return; }
    g_listenSock = ls;
    FileLog("socket en ecoute sur 127.0.0.1:%u", kPort);

    while (g_running) {
        SOCKET cs = accept(ls, nullptr, nullptr);
        if (cs == INVALID_SOCKET) { if (!g_running) break; Sleep(50); continue; }
        BOOL nd = TRUE; setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, (const char*)&nd, sizeof(nd));
        std::string buf;
        char tmp[4096];
        while (g_running) {
            int n = recv(cs, tmp, sizeof(tmp), 0);
            if (n <= 0) break;
            buf.append(tmp, n);
            size_t nl;
            while ((nl = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, nl); buf.erase(0, nl + 1);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                if (line.empty()) continue;
                std::string reply = HandleLine(line);
                size_t sent = 0;
                while (sent < reply.size()) {
                    int s = send(cs, reply.c_str() + sent, (int)(reply.size() - sent), 0);
                    if (s <= 0) break; sent += s;
                }
            }
        }
        closesocket(cs);
    }
    closesocket(ls); g_listenSock = INVALID_SOCKET;
    WSACleanup();
    FileLog("thread socket termine");
}

// ---------------------------------------------------------------------------------------------
// IModuleInterface — ordre des virtuelles verifie (layouts.md : Startup=1, Shutdown=4, AutoShutdown=6)
// ---------------------------------------------------------------------------------------------
class IModuleInterface {
public:
    virtual ~IModuleInterface() {}
    virtual void StartupModule() {}
    virtual void PreUnloadCallback() {}
    virtual void PostLoadCallback() {}
    virtual void ShutdownModule() {}
    virtual bool SupportsDynamicReloading() { return true; }
    virtual bool SupportsAutomaticShutdown() { return true; }
    virtual bool IsGameModule() const { return false; }
};

class FDevKitApiModule : public IModuleInterface {
public:
    void StartupModule() override
    {
        g_mainThreadId = GetCurrentThreadId();
        FileLog("StartupModule (main thread %lu, editor_ready=%d)", g_mainThreadId, EditorReady() ? 1 : 0);
        LoadWhitelistFile();
        {
            std::wstring w = L"[DevKitApi] write whitelist (" + std::wstring(g_whitelistSource.begin(), g_whitelistSource.end()) + L") : ";
            std::string l = WriteRootsList(); w += std::wstring(l.begin(), l.end());
            EngineLog(ELogVerbosity_Log, w.c_str());
        }
        g_running = true;
        g_hook = SetWindowsHookExW(WH_GETMESSAGE, HookProc, g_hInstance, g_mainThreadId);
        if (!g_hook) FileLog("SetWindowsHookExW failed %lu", GetLastError());
        g_socketThread = std::thread(SocketThreadMain);
        EngineLog(ELogVerbosity_Log, L"[DevKitApi] loaded (socket 127.0.0.1:9877)");
        FileLog("[DevKitApi] loaded");
    }
    void ShutdownModule() override
    {
        FileLog("ShutdownModule");
        g_running = false;
        if (g_listenSock != INVALID_SOCKET) closesocket(g_listenSock);   // debloque accept()
        if (g_socketThread.joinable()) g_socketThread.join();
        if (g_hook) { UnhookWindowsHookEx(g_hook); g_hook = nullptr; }
        // Commandes restantes : on les libere (les clients ont deja eu leur timeout)
        std::lock_guard<std::mutex> lk(g_queueMutex);
        for (Command* c : g_queue) { CloseHandle(c->done); delete c; }
        g_queue.clear();
    }
    bool SupportsDynamicReloading() override { return true; }
    bool SupportsAutomaticShutdown() override { return true; }
};

extern "C" __declspec(dllexport) IModuleInterface* InitializeModule()
{
    FileLog("InitializeModule (FMemory %s)", Alloc().useCrt ? "ABSENT -> CRT" : "OK");
    return new FDevKitApiModule();
}

BOOL WINAPI DllMain(HINSTANCE hinst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) { g_hInstance = hinst; DisableThreadLibraryCalls(hinst); }
    return TRUE;
}
