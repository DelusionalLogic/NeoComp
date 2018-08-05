#include "bezier.h"
#include <stdint.h>
#include <math.h>

#define NEWTON_ITERATIONS          4
#define NEWTON_MIN_SLOPE           0.02
#define SUBDIVISION_PRECISION      0.0000001
#define SUBDIVISION_MAX_ITERATIONS 10

const double SampleStepSize = 1.0 / (double)(SPLINE_TABLE_SIZE - 1);

static double A(double aA1, double aA2) {
    return 1.0 - 3.0 * aA2 + 3.0 * aA1;
}

static double B(double aA1, double aA2) {
    return 3.0 * aA2 - 6.0 * aA1;
}

static double C(double aA1) {
    return 3.0 * aA1;
}

static double bezier_calculate(struct Bezier* bezier, double aT, double aA1, double aA2) {
    // use Horner's scheme to evaluate the Bezier polynomial
    return ((A(aA1, aA2)*aT + B(aA1, aA2))*aT + C(aA1))*aT;
}

static double bezier_getSlope(struct Bezier* bezier, double aT, double aA1, double aA2) {
    return 3.0 * A(aA1, aA2)*aT*aT + 2.0 * B(aA1, aA2) * aT + C(aA1);
}

static void calculate_samples(struct Bezier* bezier) {
    for (uint32_t i = 0; i < SPLINE_TABLE_SIZE; i++) {
        double val = (double)i;
        bezier->samples[i] = bezier_calculate(bezier, val * SampleStepSize, bezier->x1, bezier->x2);
    }
}

void bezier_init(struct Bezier* bezier, double aX1, double aY1, double aX2, double aY2) {
    bezier->x1 = aX1;
    bezier->y1 = aY1;
    bezier->x2 = aX2;
    bezier->y2 = aY2;

    if (bezier->x1 != bezier->y1 || bezier->x2 != bezier->y2)
        calculate_samples(bezier);
}

double bezier_getSplineValue(struct Bezier* bezier, double aX) {
    if (bezier->x1 == bezier->y1 && bezier->x2 == bezier->y2)
        return aX;

    return bezier_calculate(bezier, bezier_getTForX(bezier, aX), bezier->y1, bezier->y2);
}

void bezier_getSplineDerivatives(struct Bezier* bezier, double aX, double* aDX, double* aDY) {
    double t = bezier_getTForX(bezier, aX);
    *aDX = bezier_getSlope(bezier, t, bezier->x1, bezier->x2);
    *aDY = bezier_getSlope(bezier, t, bezier->y1, bezier->y2);
}

double bezier_getTForX(struct Bezier* bezier, double aX) {
    // Early return when aX == 1.0 to avoid floating-point inaccuracies.
    if (aX == 1.0) {
        return 1.0;
    }
    // Find interval where t lies
    double intervalStart = 0.0;
    const double* currentSample = &bezier->samples[1];
    const double* const lastSample = &bezier->samples[SPLINE_TABLE_SIZE - 1];
    for (; currentSample != lastSample && *currentSample <= aX; ++currentSample) {
        intervalStart += SampleStepSize;
    }
    --currentSample; // t now lies between *currentSample and *currentSample+1

    // Interpolate to provide an initial guess for t
    double dist = (aX - *currentSample) /
        (*(currentSample+1) - *currentSample);
    double guessForT = intervalStart + dist * SampleStepSize;

    // Check the slope to see what strategy to use. If the slope is too small
    // Newton-Raphson iteration won't converge on a root so we use bisection
    // instead.
    double initialSlope = bezier_getSlope(bezier, guessForT, bezier->x1, bezier->x2);
    if (initialSlope >= NEWTON_MIN_SLOPE) {
        return bezier_newtonRaphsonIterate(bezier, aX, guessForT);
    } else if (initialSlope == 0.0) {
        return guessForT;
    } else {
        return bezier_binarySubdivide(bezier, aX, intervalStart, intervalStart + SampleStepSize);
    }
}

double bezier_newtonRaphsonIterate(struct Bezier* bezier, double aX, double aGuessT) {
    // Refine guess with Newton-Raphson iteration
    for (uint32_t i = 0; i < NEWTON_ITERATIONS; ++i) {
        // We're trying to find where f(t) = aX,
        // so we're actually looking for a root for: CalcBezier(t) - aX
        double currentX = bezier_calculate(bezier, aGuessT, bezier->x1, bezier->x2) - aX;
        double currentSlope = bezier_getSlope(bezier, aGuessT, bezier->x1, bezier->x2);

        if (currentSlope == 0.0)
            return aGuessT;

        aGuessT -= currentX / currentSlope;
    }

    return aGuessT;
}

double bezier_binarySubdivide(struct Bezier* bezier, double aX, double aA, double aB) {
    double currentX;
    double currentT;
    uint32_t i = 0;

    do
    {
        currentT = aA + (aB - aA) / 2.0;
        currentX = bezier_calculate(bezier, currentT, bezier->x1, bezier->x2) - aX;

        if (currentX > 0.0) {
            aB = currentT;
        } else {
            aA = currentT;
        }
    } while (fabs(currentX) > SUBDIVISION_PRECISION
            && ++i < SUBDIVISION_MAX_ITERATIONS);

    return currentT;
}
