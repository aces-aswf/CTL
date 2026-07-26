///////////////////////////////////////////////////////////////////////////
// Copyright Contributors to the CTL project.
// SPDX-License-Identifier: BSD-3-Clause
///////////////////////////////////////////////////////////////////////////

//
// Regression fixtures for `input uniform bool` default-value handling.
//
// Background: a long-standing bug report (originating in the LightSpace +
// ACES 1.x era) claimed that `input uniform bool legalRange = false` in
// the SMPTE-legal-range ODT path produced random per-pixel results -- the
// interpreter was alleged to read the boolean as random true/false despite
// the declared default.  The workaround at the time was to remove the
// `legalRange` parameter from the ACES ODT and use a separate transform
// for legal-range output.
//
// We have not been able to reproduce that exact symptom on the current
// SIMD interpreter.  These fixtures lock in the *correct* observable
// behaviour so any regression is caught immediately by CI.
//
// Functions:
//   ub_default_true   -- default=true,  TRUE branch returns 1.0,  FALSE returns 0.0
//   ub_default_false  -- default=false, TRUE branch returns 1.0,  FALSE returns 0.0
//   ub_two_bools      -- two adjacent uniform bools, returns a 2-bit packed code
//   ub_with_varying   -- uniform bool alongside varying float input

namespace ub_test
{

void
ub_default_true (output float r, input uniform bool flag = true)
{
    if (flag) r = 1.0;
    else      r = 0.0;
}

void
ub_default_false (output float r, input uniform bool flag = false)
{
    if (flag) r = 1.0;
    else      r = 0.0;
}

//
// Two adjacent uniform bools.  Encodes both into a single float so we
// can verify each independently in one call:
//   r = 0.0  means flagA=false, flagB=false
//   r = 1.0  means flagA=true,  flagB=false
//   r = 2.0  means flagA=false, flagB=true
//   r = 3.0  means flagA=true,  flagB=true
//
void
ub_two_bools (output float r,
              input uniform bool flagA = true,
              input uniform bool flagB = false)
{
    float a = 0.0;
    float b = 0.0;
    if (flagA) a = 1.0;
    if (flagB) b = 2.0;
    r = a + b;
}

//
// Uniform bool alongside a per-pixel varying float input.  Output =
// `value` (varying) when flag is true, 0.0 otherwise.  Verifies the
// uniform bool is broadcast correctly across all SIMD lanes.
//
void
ub_with_varying (output float r,
                 input float value,
                 input uniform bool flag = true)
{
    if (flag) r = value;
    else      r = 0.0;
}

} // namespace ub_test
