#pragma once

#ifdef _MSC_VER
    #ifndef TAI_INLINE
        #define TAI_INLINE __forceinline
    #endif
    #ifndef TAI_HIDDEN
        #define TAI_HIDDEN
    #endif
    #ifndef TAI_EXPORT
        #define TAI_EXPORT __forceinline __declspec(dllexport)
    #endif
#else
    #ifndef TAI_INLINE
//        #define TAI_INLINE __attribute__((__always_inline__)) __attribute__((visibility("hidden")))
        #define TAI_INLINE __attribute__((visibility("hidden")))
    #endif
    #ifndef TAI_HIDDEN
        #define TAI_HIDDEN __attribute__((visibility("hidden")))
    #endif
    #ifndef TAI_EXPORT
        #define TAI_EXPORT __attribute__((__always_inline__)) __attribute__((visibility("public")))
    #endif
#endif

namespace tai
{
    class BTreeConfig;
    class BTreeNodeBase;
    class BTreeBase;
    class IOEngine;
    class IOCtrl;
    class TLQ;
    class Controller;
    class Worker;
}
