use crate::{JSCell, JSValue};
#[cfg(target_pointer_width = "64")]
use crate::ffi;

/// ABI-compatible with `JSC::JSValue`.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct DecodedJSValue {
    pub u: EncodedValueDescriptor,
}

/// ABI-compatible with `JSC::EncodedValueDescriptor`.
#[repr(C)]
#[derive(Copy, Clone)]
pub union EncodedValueDescriptor {
    pub as_int64: i64,
    pub ptr: *mut JSCell,
    pub as_bits: AsBits,
}

#[repr(C)]
#[derive(Copy, Clone)]
pub struct AsBits {
    pub payload: i32,
    pub tag: i32,
}

impl DecodedJSValue {
    /// Raw 64-bit encoding. Safe: every `EncodedValueDescriptor` arm is an
    /// 8-byte POD scalar with no invalid bit patterns, so the `i64` view is
    /// always initialized regardless of which arm wrote it.
    #[inline]
    fn bits(self) -> i64 {
        // SAFETY: `#[repr(C)]` union of 8-byte POD scalars (i64 / `*mut JSCell`
        // / `AsBits{i32,i32}`); reading `as_int64` is a same-size reinterpret
        // into a fully-inhabited type.
        unsafe { self.u.as_int64 }
    }

    /// Equivalent to `JSC::JSValue::encode`.
    pub fn encode(self) -> JSValue {
        JSValue::from_raw(self.bits())
    }

    fn as_u64(self) -> u64 {
        self.bits() as u64
    }

    /// Equivalent to `JSC::JSValue::isCell`. Note that like JSC, this method treats 0 as a cell.
    pub fn is_cell(self) -> bool {
        #[cfg(target_pointer_width = "64")]
        {
            self.as_u64() & ffi::NOT_CELL_MASK == 0
        }
        #[cfg(target_pointer_width = "32")]
        {
            // SAFETY: `as_bits` is the tag/payload view of the 8-byte encoding.
            unsafe { self.u.as_bits.tag as u32 == 0xffff_fffb }
        }
    }

    /// Equivalent to `JSC::JSValue::asCell`.
    pub fn as_cell(self) -> *mut JSCell {
        debug_assert!(self.is_cell(), "not a cell: 0x{:x}", self.as_u64());
        // is_cell() guarantees the encoded bits ARE the (possibly-null) JSCell
        // pointer; safe int→ptr `as` cast replaces the union pun (same idiom as
        // `JSValue::as_ptr` — provenance is FFI-exposed by JSC's C++ side).
        #[cfg(target_pointer_width = "64")]
        {
            self.bits() as usize as *mut JSCell
        }
        #[cfg(target_pointer_width = "32")]
        {
            // SAFETY: `as_bits.payload` is the low 32-bit cell pointer.
            unsafe { self.u.as_bits.payload as u32 as usize as *mut JSCell }
        }
    }
}

#[cfg(target_pointer_width = "64")]
const _: () = assert!(
    core::mem::size_of::<usize>() == 8,
    "EncodedValueDescriptor assumes a 64-bit system",
);
const _: () = assert!(
    cfg!(target_endian = "little"),
    "EncodedValueDescriptor.as_bits assumes a little-endian system",
);
