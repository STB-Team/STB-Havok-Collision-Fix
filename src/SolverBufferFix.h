#pragma once

// Before an island of touching bodies is solved (SE: REL::ID 61420, called from the continuous simulation 60941),
// Havok sizes the solver's schema and temp buffers from the island's running totals hkpConstraintInfo
// (m_sizeOfSchemas, m_numSolverElemTemps). Those totals are updated incrementally as constraints come and go and
// can end up lower than what the constraints really write; the solver then runs past the end of its buffer:
// EXCEPTION_BREAKPOINT in the schema loop or an access violation in the following step.
// The fix gives both buffers a reserve for that one call and takes it back afterwards; the solver math is unchanged.
namespace SolverBufferFix
{
	void Install();
}
