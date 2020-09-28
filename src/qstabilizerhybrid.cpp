//////////////////////////////////////////////////////////////////////////////////////
//
// (C) Daniel Strano and the Qrack contributors 2017-2020. All rights reserved.
//
// QPager breaks a QEngine instance into pages of contiguous amplitudes.
//
// Licensed under the GNU Lesser General Public License V3.
// See LICENSE.md in the project root or https://www.gnu.org/licenses/lgpl-3.0.en.html
// for details.

#include <thread>

#include "qfactory.hpp"
#include "qstabilizerhybrid.hpp"

namespace Qrack {

QStabilizerHybrid::QStabilizerHybrid(QInterfaceEngine eng, QInterfaceEngine subEng, bitLenInt qBitCount,
    bitCapInt initState, qrack_rand_gen_ptr rgp, complex phaseFac, bool doNorm, bool randomGlobalPhase, bool useHostMem,
    int deviceId, bool useHardwareRNG, bool useSparseStateVec, real1 norm_thresh, std::vector<int> ignored,
    bitLenInt qubitThreshold)
    : QInterface(qBitCount, rgp, doNorm, useHardwareRNG, randomGlobalPhase, norm_thresh)
    , engineType(eng)
    , subEngineType(subEng)
    , engine(NULL)
    , devID(deviceId)
    , phaseFactor(phaseFac)
    , doNormalize(doNorm)
    , useHostRam(useHostMem)
    , useRDRAND(useHardwareRNG)
    , isSparse(useSparseStateVec)
    , thresholdQubits(qubitThreshold)
{
    concurrency = std::thread::hardware_concurrency();
    stabilizer = MakeStabilizer(initState);
}

QStabilizerPtr QStabilizerHybrid::MakeStabilizer(const bitCapInt& perm)
{
    return std::make_shared<QStabilizer>(qubitCount, perm, useRDRAND, rand_generator);
}

QInterfacePtr QStabilizerHybrid::MakeEngine(const bitCapInt& perm)
{
    QInterfacePtr toRet = CreateQuantumInterface(engineType, subEngineType, qubitCount, 0, rand_generator, phaseFactor,
        doNormalize, randGlobalPhase, useHostRam, devID, useRDRAND, isSparse, amplitudeFloor, std::vector<int>{},
        thresholdQubits);
    toRet->SetConcurrency(concurrency);
    return toRet;
}

QInterfacePtr QStabilizerHybrid::Clone()
{
    Finish();

    QStabilizerHybridPtr c =
        std::dynamic_pointer_cast<QStabilizerHybrid>(CreateQuantumInterface(QINTERFACE_STABILIZER_HYBRID, engineType,
            subEngineType, qubitCount, 0, rand_generator, phaseFactor, doNormalize, randGlobalPhase, useHostRam, devID,
            useRDRAND, isSparse, amplitudeFloor, std::vector<int>{}, thresholdQubits));

    if (stabilizer) {
        c->stabilizer = std::make_shared<QStabilizer>(*stabilizer);
    } else {
        complex* stateVec = new complex[maxQPower];
        engine->GetQuantumState(stateVec);
        c->SwitchToEngine();
        c->engine->SetQuantumState(stateVec);
        delete[] stateVec;
    }

    return c;
}

void QStabilizerHybrid::SwitchToEngine()
{
    if (engine) {
        return;
    }

    complex* stateVec = new complex[maxQPower];
    stabilizer->GetQuantumState(stateVec);

    engine = MakeEngine();
    engine->SetQuantumState(stateVec);
    delete[] stateVec;

    if (engineType != QINTERFACE_QUNIT) {
        stabilizer.reset();
        return;
    }

    for (bitLenInt i = 0; i < qubitCount; i++) {
        if (stabilizer->IsSeparableZ(i)) {
            engine->SetBit(i, stabilizer->M(i));
            continue;
        }

        stabilizer->H(i);
        if (stabilizer->IsSeparableZ(i)) {
            engine->SetBit(i, stabilizer->M(i));
            engine->H(i);
            continue;
        }

        stabilizer->S(i);
        if (stabilizer->IsSeparableZ(i)) {
            engine->SetBit(i, stabilizer->M(i));
            engine->H(i);
            engine->S(i);
        }
    }

    stabilizer.reset();
}

void QStabilizerHybrid::CCNOT(bitLenInt control1, bitLenInt control2, bitLenInt target)
{
    if (stabilizer) {
        real1 prob = Prob(control1);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->CNOT(control2, target);
            return;
        }

        prob = Prob(control2);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->CNOT(control1, target);
            return;
        }

        SwitchToEngine();
    }

    engine->CCNOT(control1, control2, target);
}

void QStabilizerHybrid::CH(bitLenInt control, bitLenInt target)
{
    if (stabilizer) {
        real1 prob = Prob(control);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->H(target);
            return;
        }

        SwitchToEngine();
    }

    engine->CH(control, target);
}

void QStabilizerHybrid::CS(bitLenInt control, bitLenInt target)
{
    if (stabilizer) {
        real1 prob = Prob(control);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->S(target);
            return;
        }

        SwitchToEngine();
    }

    engine->CS(control, target);
}

void QStabilizerHybrid::CIS(bitLenInt control, bitLenInt target)
{
    if (stabilizer) {
        real1 prob = Prob(control);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->IS(target);
            return;
        }

        SwitchToEngine();
    }

    engine->CIS(control, target);
}

void QStabilizerHybrid::CCZ(bitLenInt control1, bitLenInt control2, bitLenInt target)
{
    if (stabilizer) {
        real1 prob = Prob(control1);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->CZ(control2, target);
            return;
        }

        prob = Prob(control2);
        if (prob == ZERO_R1) {
            return;
        }
        if (prob == ONE_R1) {
            stabilizer->CZ(control1, target);
            return;
        }

        SwitchToEngine();
    }

    engine->CCZ(control1, control2, target);
}

void QStabilizerHybrid::Decompose(bitLenInt start, QStabilizerHybridPtr dest)
{
    bitLenInt length = dest->qubitCount;

    if (length == qubitCount) {
        dest->stabilizer = stabilizer;
        stabilizer = NULL;
        dest->engine = engine;
        engine = NULL;

        SetQubitCount(1);
        stabilizer = MakeStabilizer(0);
        return;
    }

    if (engine) {
        dest->SwitchToEngine();
        engine->Decompose(start, dest->engine);
        SetQubitCount(qubitCount - length);
        return;
    }

    if (dest->engine) {
        dest->engine.reset();
        dest->stabilizer = dest->MakeStabilizer(0);
    }

    stabilizer->Decompose(start, dest->stabilizer);
    SetQubitCount(qubitCount - length);
}

void QStabilizerHybrid::Dispose(bitLenInt start, bitLenInt length)
{
    if (length == qubitCount) {
        stabilizer = NULL;
        engine = NULL;

        SetQubitCount(1);
        stabilizer = MakeStabilizer(0);
        return;
    }

    if (engine) {
        engine->Dispose(start, length);
    } else {
        stabilizer->Dispose(start, length);
    }

    SetQubitCount(qubitCount - length);
}

void QStabilizerHybrid::Dispose(bitLenInt start, bitLenInt length, bitCapInt disposedPerm)
{
    if (length == qubitCount) {
        stabilizer = NULL;
        engine = NULL;

        SetQubitCount(1);
        stabilizer = MakeStabilizer(0);
        return;
    }

    if (engine) {
        engine->Dispose(start, length, disposedPerm);
    } else {
        stabilizer->Dispose(start, length);
    }

    SetQubitCount(qubitCount - length);
}

void QStabilizerHybrid::SetQuantumState(const complex* inputState)
{
    if (qubitCount == 1U) {
        bool isClifford = false;
        bool isSet;
        bool isX = false;
        bool isY = false;
        if (inputState[1] == ZERO_CMPLX) {
            isClifford = true;
            isSet = false;
        } else if (inputState[0] == ZERO_CMPLX) {
            isClifford = true;
            isSet = true;
        } else if (inputState[0] == inputState[1]) {
            isClifford = true;
            isSet = false;
            isX = true;
        } else if (inputState[0] == -inputState[1]) {
            isClifford = true;
            isSet = true;
            isX = true;
        } else if ((I_CMPLX * inputState[0]) == inputState[1]) {
            isClifford = true;
            isSet = false;
            isY = true;
        } else if ((I_CMPLX * inputState[0]) == -inputState[1]) {
            isClifford = true;
            isSet = true;
            isY = true;
        }

        if (isClifford) {
            engine.reset();
            if (stabilizer) {
                stabilizer->SetPermutation(isSet ? 1 : 0);
            } else {
                stabilizer = MakeStabilizer(isSet ? 1 : 0);
            }
            if (isX || isY) {
                stabilizer->H(0);
            }
            if (isY) {
                stabilizer->S(0);
            }
            return;
        }
    }

    SwitchToEngine();
    engine->SetQuantumState(inputState);
}

void QStabilizerHybrid::GetProbs(real1* outputProbs)
{
    if (stabilizer) {
        complex* stateVec = new complex[maxQPower];
        stabilizer->GetQuantumState(stateVec);
        for (bitCapInt i = 0; i < maxQPower; i++) {
            outputProbs[i] = norm(stateVec[i]);
        }
        delete[] stateVec;
    } else {
        engine->GetProbs(outputProbs);
    }
}

void QStabilizerHybrid::ApplySingleBit(const complex* mtrx, bitLenInt target)
{
    if (IsIdentity(mtrx, true)) {
        return;
    }

    if ((norm(mtrx[1]) == 0) && (norm(mtrx[2]) == 0)) {
        ApplySinglePhase(mtrx[0], mtrx[3], target);
        return;
    }
    if ((norm(mtrx[0]) == 0) && (norm(mtrx[3]) == 0)) {
        ApplySingleInvert(mtrx[1], mtrx[2], target);
        return;
    }
    if ((mtrx[0] == complex(M_SQRT1_2, ZERO_R1)) && (mtrx[0] == mtrx[1]) && (mtrx[0] == mtrx[2]) &&
        (mtrx[2] == -mtrx[3])) {
        H(target);
        return;
    }

    SwitchToEngine();
    engine->ApplySingleBit(mtrx, target);
}

void QStabilizerHybrid::ApplySinglePhase(const complex topLeft, const complex bottomRight, bitLenInt target)
{
    if (engine) {
        engine->ApplySinglePhase(topLeft, bottomRight, target);
        return;
    }

    if (topLeft == bottomRight) {
        return;
    }

    if (topLeft == -bottomRight) {
        stabilizer->Z(target);
        return;
    }

    complex sTest = bottomRight / topLeft;

    if (sTest == I_CMPLX) {
        stabilizer->S(target);
        return;
    }

    if (sTest == -I_CMPLX) {
        stabilizer->IS(target);
        return;
    }

    SwitchToEngine();
    engine->ApplySinglePhase(topLeft, bottomRight, target);
}

void QStabilizerHybrid::ApplySingleInvert(const complex topRight, const complex bottomLeft, bitLenInt target)
{
    if (engine) {
        engine->ApplySingleInvert(topRight, bottomLeft, target);
        return;
    }

    if (topRight == bottomLeft) {
        stabilizer->X(target);
        return;
    }

    if (topRight == -bottomLeft) {
        stabilizer->Z(target);
        stabilizer->X(target);
        return;
    }

    complex sTest = topRight / bottomLeft;

    if (sTest == I_CMPLX) {
        stabilizer->S(target);
        stabilizer->X(target);
        return;
    }

    if (sTest == -I_CMPLX) {
        stabilizer->IS(target);
        stabilizer->X(target);
        return;
    }

    SwitchToEngine();
    engine->ApplySingleInvert(topRight, bottomLeft, target);
}

void QStabilizerHybrid::ApplyControlledSingleBit(
    const bitLenInt* controls, const bitLenInt& controlLen, const bitLenInt& target, const complex* mtrx)
{
    if (!controlLen) {
        ApplySingleBit(mtrx, target);
        return;
    }

    if (engine) {
        engine->ApplyControlledSingleBit(controls, controlLen, target, mtrx);
        return;
    }

    if (!norm(mtrx[1]) && !norm(mtrx[2])) {
        ApplyControlledSinglePhase(controls, controlLen, target, mtrx[0], mtrx[3]);
        return;
    }

    if (!norm(mtrx[0]) && !norm(mtrx[3])) {
        ApplyControlledSingleInvert(controls, controlLen, target, mtrx[1], mtrx[2]);
        return;
    }

    if ((controlLen == 1U) && (mtrx[0] == complex(M_SQRT1_2, ZERO_R1)) && (mtrx[0] == mtrx[1]) &&
        (mtrx[0] == mtrx[2]) && (mtrx[2] == -mtrx[3])) {
        CH(controls[0], target);
        return;
    }

    SwitchToEngine();
    engine->ApplyControlledSingleBit(controls, controlLen, target, mtrx);
}

void QStabilizerHybrid::ApplyControlledSinglePhase(const bitLenInt* controls, const bitLenInt& controlLen,
    const bitLenInt& target, const complex topLeft, const complex bottomRight)
{
    if (!controlLen) {
        ApplySinglePhase(topLeft, bottomRight, target);
        return;
    }

    // TODO: Generalize to trim all possible controls, like in QUnit.
    if ((controlLen == 2U) && (topLeft == ONE_CMPLX) && (bottomRight == -ONE_CMPLX)) {
        CCZ(controls[0], controls[1], target);
        return;
    }

    if ((topLeft != ONE_CMPLX) || (controlLen > 1U)) {
        SwitchToEngine();
    }

    if (engine) {
        engine->ApplyControlledSinglePhase(controls, controlLen, target, topLeft, bottomRight);
        return;
    }

    if (bottomRight == I_CMPLX) {
        CS(controls[0], target);
        return;
    }

    if (bottomRight == -I_CMPLX) {
        CIS(controls[0], target);
        return;
    }

    if (bottomRight == ONE_CMPLX) {
        return;
    }

    if (bottomRight == -ONE_CMPLX) {
        stabilizer->CZ(controls[0], target);
        return;
    }

    SwitchToEngine();
    engine->ApplyControlledSinglePhase(controls, controlLen, target, topLeft, bottomRight);
}

void QStabilizerHybrid::ApplyControlledSingleInvert(const bitLenInt* controls, const bitLenInt& controlLen,
    const bitLenInt& target, const complex topRight, const complex bottomLeft)
{
    if (!controlLen) {
        ApplySingleInvert(topRight, bottomLeft, target);
        return;
    }

    // TODO: Generalize to trim all possible controls, like in QUnit.
    if ((controlLen == 2U) && (topRight == ONE_CMPLX) && (bottomLeft == ONE_CMPLX)) {
        CCNOT(controls[0], controls[1], target);
        return;
    }

    if (controlLen > 1U) {
        SwitchToEngine();
    }

    if (engine) {
        engine->ApplyControlledSingleInvert(controls, controlLen, target, topRight, bottomLeft);
        return;
    }

    if ((topRight == ONE_CMPLX) && (bottomLeft == ONE_CMPLX)) {
        stabilizer->CNOT(controls[0], target);
        return;
    }

    if ((topRight == ONE_CMPLX) && (bottomLeft == -ONE_CMPLX)) {
        stabilizer->CNOT(controls[0], target);
        stabilizer->CZ(controls[0], target);
        return;
    }

    if ((topRight == -ONE_CMPLX) && (bottomLeft == ONE_CMPLX)) {
        stabilizer->CZ(controls[0], target);
        stabilizer->CNOT(controls[0], target);
        return;
    }

    if ((topRight == -ONE_CMPLX) && (bottomLeft == -ONE_CMPLX)) {
        stabilizer->CZ(controls[0], target);
        stabilizer->CNOT(controls[0], target);
        stabilizer->CZ(controls[0], target);
        return;
    }

    SwitchToEngine();
    engine->ApplyControlledSingleInvert(controls, controlLen, target, topRight, bottomLeft);
}

void QStabilizerHybrid::ApplyAntiControlledSingleBit(
    const bitLenInt* controls, const bitLenInt& controlLen, const bitLenInt& target, const complex* mtrx)
{
    if (!controlLen) {
        ApplySingleBit(mtrx, target);
        return;
    }

    if (controlLen > 1U) {
        SwitchToEngine();
    }

    if (engine) {
        engine->ApplyAntiControlledSingleBit(controls, controlLen, target, mtrx);
        return;
    }

    if (!norm(mtrx[1]) && !norm(mtrx[2])) {
        ApplyAntiControlledSinglePhase(controls, controlLen, target, mtrx[0], mtrx[3]);
        return;
    }

    if (!norm(mtrx[0]) && !norm(mtrx[3])) {
        ApplyAntiControlledSingleInvert(controls, controlLen, target, mtrx[1], mtrx[2]);
        return;
    }

    SwitchToEngine();
    engine->ApplyAntiControlledSingleBit(controls, controlLen, target, mtrx);
}

void QStabilizerHybrid::ApplyAntiControlledSinglePhase(const bitLenInt* controls, const bitLenInt& controlLen,
    const bitLenInt& target, const complex topLeft, const complex bottomRight)
{
    if (!controlLen) {
        ApplySinglePhase(topLeft, bottomRight, target);
        return;
    }

    if ((topLeft != ONE_CMPLX) || (controlLen > 1U)) {
        SwitchToEngine();
    }

    if (engine) {
        engine->ApplyAntiControlledSinglePhase(controls, controlLen, target, topLeft, bottomRight);
        return;
    }

    if (bottomRight == I_CMPLX) {
        X(controls[0]);
        CS(controls[0], target);
        X(controls[0]);
        return;
    }

    if (bottomRight == -I_CMPLX) {
        X(controls[0]);
        CIS(controls[0], target);
        X(controls[0]);
        return;
    }

    if (bottomRight == ONE_CMPLX) {
        return;
    }

    if (bottomRight == -ONE_CMPLX) {
        stabilizer->X(controls[0]);
        stabilizer->CZ(controls[0], target);
        stabilizer->X(controls[0]);
        return;
    }

    SwitchToEngine();
    engine->ApplyAntiControlledSinglePhase(controls, controlLen, target, topLeft, bottomRight);
}

void QStabilizerHybrid::ApplyAntiControlledSingleInvert(const bitLenInt* controls, const bitLenInt& controlLen,
    const bitLenInt& target, const complex topRight, const complex bottomLeft)
{
    if (!controlLen) {
        ApplySingleInvert(topRight, bottomLeft, target);
        return;
    }

    if (((topRight != ONE_CMPLX) && (bottomLeft != ONE_CMPLX)) || (controlLen > 1U)) {
        SwitchToEngine();
    }

    if (engine) {
        engine->ApplyAntiControlledSingleInvert(controls, controlLen, target, topRight, bottomLeft);
        return;
    }

    if ((topRight == ONE_CMPLX) && (bottomLeft == ONE_CMPLX)) {
        stabilizer->X(controls[0]);
        stabilizer->CNOT(controls[0], target);
        stabilizer->X(controls[0]);
        return;
    }

    if ((topRight == ONE_CMPLX) && (bottomLeft == -ONE_CMPLX)) {
        stabilizer->X(controls[0]);
        stabilizer->CNOT(controls[0], target);
        stabilizer->CZ(controls[0], target);
        stabilizer->X(controls[0]);
        return;
    }

    if ((topRight == -ONE_CMPLX) && (bottomLeft == ONE_CMPLX)) {
        stabilizer->X(controls[0]);
        stabilizer->CZ(controls[0], target);
        stabilizer->CNOT(controls[0], target);
        stabilizer->X(controls[0]);
        return;
    }

    SwitchToEngine();
    engine->ApplyAntiControlledSingleInvert(controls, controlLen, target, topRight, bottomLeft);
}
} // namespace Qrack