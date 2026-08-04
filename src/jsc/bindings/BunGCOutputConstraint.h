#pragma once

#if CPU(ADDRESS64)
#include <JavaScriptCore/MarkingConstraint.h>

namespace JSC {
class VM;
}

namespace WebCore {

class JSHeapData;

class DOMGCOutputConstraint : public JSC::MarkingConstraint {
    WTF_DEPRECATED_MAKE_FAST_ALLOCATED(DOMGCOutputConstraint);

public:
    DOMGCOutputConstraint(JSC::VM&, JSHeapData&);
    ~DOMGCOutputConstraint();

protected:
    void executeImpl(JSC::AbstractSlotVisitor&) override;
    void executeImpl(JSC::SlotVisitor&) override;

private:
    template<typename Visitor> void executeImplImpl(Visitor&);

    JSC::VM& m_vm;
    JSHeapData& m_heapData;
    uint64_t m_lastExecutionVersion;
};

} // namespace WebCore

#endif // CPU(ADDRESS64)
