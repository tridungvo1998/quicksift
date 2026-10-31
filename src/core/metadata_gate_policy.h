// CODE GUIDE: See CODE_GUIDE.md -> "Where new code goes".
#pragma once
// Admission rule of the process-wide Exiv2 gate (embedded_metadata.cpp), portable
// so scripts/checks/sidecar_write_checks.cpp can pin it.
//
// One holder at a time. A pending metadata write (writeInterest > 0) makes new
// background readers wait so the write is not starved. The thread that runs the
// write must still be able to READ (the sidecar writer reads the RAW's embedded
// values once to fill fields the sidecar lacks): before 0.9.5.0 that read waited
// for writeInterest == 0, but the interest was held by the same write, so RAW
// rating/pick writes hung forever on the metadata service thread.
namespace quicksift::core::metadata_gate {

[[nodiscard]] constexpr bool Admit(bool write, bool writerThread, int holders, int writeInterest) noexcept {
    if (holders != 0) return false;
    return write || writerThread || writeInterest == 0;
}

} // namespace quicksift::core::metadata_gate
