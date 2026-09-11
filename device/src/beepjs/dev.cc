#include "beepjs.h"

#include <malloc.h>
#include <js/MemoryMetrics.h>


namespace {
namespace JSDev {

static JSBool PrintMemoryMetrics(JSContext *cx, unsigned argc, jsval *vp);

static JSClass Class = {
    "dev",
    0,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_PropertyStub,
    JS_StrictPropertyStub,
    JS_EnumerateStub,
    JS_ResolveStub,
    JS_ConvertStub,
    NULL
};

static JSFunctionSpec Funcs[] = {
    JS_FS("printMemoryMetrics", PrintMemoryMetrics, 0, JSPROP_ENUMERATE),
    JS_FS_END
};
}  // namespace JSDev

class MyRuntimeStats : public JS::RuntimeStats {
public:
    MyRuntimeStats(JSMallocSizeOfFun mallocSizeOf)
      : JS::RuntimeStats(mallocSizeOf)
    {}
    virtual void initExtraCompartmentStats(
            JSCompartment *c, JS::CompartmentStats *cstats)
    {}
};


class MyObjectPrivateVisitor : public JS::ObjectPrivateVisitor {
public:
    virtual size_t sizeOfIncludingThis(void *aSupports)
    { return 1; }
};

size_t mallocSizeOf(const void *p) {
    return malloc_usable_size((void*) p);
}

JSBool JSDev::PrintMemoryMetrics(JSContext *cx, unsigned argc, jsval *vp) {
    MyObjectPrivateVisitor visitor;
    MyRuntimeStats rtStats(mallocSizeOf);
    CollectRuntimeStats(JS_GetRuntime(cx), &rtStats, &visitor);

    fprintf(stderr, "Runtime stats\n");
    fprintf(stderr, "\tgcHeapChunkTotal %zu\n", rtStats.gcHeapChunkTotal);
    fprintf(stderr, "\tgcHeapDecommittedArenas %zu\n", rtStats.gcHeapDecommittedArenas);
    fprintf(stderr, "\tgcHeapUnusedChunks %zu\n", rtStats.gcHeapUnusedChunks);
    fprintf(stderr, "\tgcHeapUnusedArena %zu\n", rtStats.gcHeapUnusedArenas);
    fprintf(stderr, "\tgcHeapChunkAdmin %zu\n", rtStats.gcHeapChunkAdmin);
    fprintf(stderr, "\tgcHeapGcThings: %zu\n", rtStats.gcHeapGcThings);

    fprintf(stderr, "Runtime sizes\n");
    fprintf(stderr, "\tobject: %zu\n", rtStats.runtime.object);
    fprintf(stderr, "\tatomsTable: %zu\n", rtStats.runtime.atomsTable);
    fprintf(stderr, "\tcontexts: %zu\n", rtStats.runtime.contexts);
    fprintf(stderr, "\tdtoa: %zu\n", rtStats.runtime.dtoa);
    fprintf(stderr, "\ttemporary: %zu\n", rtStats.runtime.temporary);
    fprintf(stderr, "\tmjitCode: %zu\n", rtStats.runtime.mjitCode);
    fprintf(stderr, "\tregexpCode: %zu\n", rtStats.runtime.regexpCode);
    fprintf(stderr, "\tunusedCodeMemory: %zu\n", rtStats.runtime.unusedCodeMemory);
    fprintf(stderr, "\tstackCommitted: %zu\n", rtStats.runtime.stackCommitted);
    fprintf(stderr, "\tgcMarker: %zu\n", rtStats.runtime.gcMarker);
    fprintf(stderr, "\tmathCache: %zu\n", rtStats.runtime.mathCache);
    fprintf(stderr, "\tscriptFilenames: %zu\n", rtStats.runtime.scriptFilenames);
    fprintf(stderr, "\tscriptSources: %zu\n", rtStats.runtime.scriptSources);

    for (size_t i = 0; i < rtStats.compartmentStatsVector.length(); i++) {
        JS::CompartmentStats &cStats = rtStats.compartmentStatsVector[i];
        fprintf(stderr, "Compartment %zu\n", i);
        fprintf(stderr, "\tgcHeapArenaAdmin: %zu\n", cStats.gcHeapArenaAdmin);
        fprintf(stderr, "\tgcHeapUnusedGcThings: %zu\n", cStats.gcHeapUnusedGcThings);
        fprintf(stderr, "\tgcHeapObjectsNonFunction: %zu\n", cStats.gcHeapObjectsNonFunction);
        fprintf(stderr, "\tgcHeapObjectsFunction: %zu\n", cStats.gcHeapObjectsFunction);
        fprintf(stderr, "\tgcHeapStrings: %zu\n", cStats.gcHeapStrings);
        fprintf(stderr, "\tgcHeapShapesTree: %zu\n", cStats.gcHeapShapesTree);
        fprintf(stderr, "\tgcHeapShapesDict: %zu\n", cStats.gcHeapShapesDict);
        fprintf(stderr, "\tgcHeapShapesBase: %zu\n", cStats.gcHeapShapesBase);
        fprintf(stderr, "\tgcHeapScripts: %zu\n", cStats.gcHeapScripts);
        fprintf(stderr, "\tgcHeapTypeObjects: %zu\n", cStats.gcHeapTypeObjects);
        fprintf(stderr, "\tobjectSlots: %zu\n", cStats.objectSlots);
        fprintf(stderr, "\tobjectElements: %zu\n", cStats.objectElements);
        fprintf(stderr, "\tobjectMisc: %zu\n", cStats.objectMisc);
        fprintf(stderr, "\tobjectPrivate: %zu\n", cStats.objectPrivate);
        fprintf(stderr, "\tstringChars: %zu\n", cStats.stringChars);
        fprintf(stderr, "\tshapesExtraTreeTables: %zu\n", cStats.shapesExtraTreeTables);
        fprintf(stderr, "\tshapesExtraDictTables: %zu\n", cStats.shapesExtraDictTables);
        fprintf(stderr, "\tshapesExtraTreeShapeKids: %zu\n", cStats.shapesExtraTreeShapeKids);
        fprintf(stderr, "\tshapesCompartmentTables: %zu\n", cStats.shapesCompartmentTables);
        fprintf(stderr, "\tscriptData: %zu\n", cStats.scriptData);
        fprintf(stderr, "\tmjitData: %zu\n", cStats.mjitData);
        fprintf(stderr, "\tcrossCompartmentWrappers: %zu\n", cStats.crossCompartmentWrappers);

        fprintf(stderr, "\ttypeInferenceSizes.scripts: %zu\n", cStats.typeInferenceSizes.scripts);
        fprintf(stderr, "\ttypeInferenceSizes.objects: %zu\n", cStats.typeInferenceSizes.objects);
        fprintf(stderr, "\ttypeInferenceSizes.tables: %zu\n", cStats.typeInferenceSizes.tables);
        fprintf(stderr, "\ttypeInferenceSizes.temporary: %zu\n", cStats.typeInferenceSizes.temporary);

    }

    JS_SET_RVAL(cx, vp, INT_TO_JSVAL(JS::UserCompartmentCount(JS_GetRuntime(cx))));
    return JS_TRUE;
}

static void fini_sys_module(JSContext *cx) {
    beepjs_delete_native_space(cx, "dev");
}

static int init_sys_module(JSContext *cx) {
    JSObject *natives_obj = beepjs_get_native_space(cx, "");
    JS::RootedObject mod_obj(cx, JS_DefineObject(cx, natives_obj, "dev",
            &JSDev::Class, NULL, JSPROP_ENUMERATE));
    if (!mod_obj) {
        return 0;
    }
    if (!JS_DefineFunctions(cx, mod_obj, JSDev::Funcs)) {
        return 0;
    }

    return 1;
}
}  // namespace


BEEPJS_MODULE_INIT(init_sys_module, 0);
BEEPJS_MODULE_FINI(fini_sys_module, 0);
