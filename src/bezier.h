#pragma once

#define SPLINE_TABLE_SIZE 11

struct Bezier {
    double x1;
    double y1;
    double x2;
    double y2;
    double samples[SPLINE_TABLE_SIZE];
};

void bezier_init(struct Bezier* bezier, double aX1, double aY1, double aX2, double aY2);
double bezier_getSplineValue(struct Bezier* bezier, double aX);
void bezier_getSplineDerivatives(struct Bezier* bezier, double aX, double* aDX, double* aDY);

double bezier_getTForX(struct Bezier* bezier, double aX);
double bezier_newtonRaphsonIterate(struct Bezier* bezier, double aX, double aGuessT);
double bezier_binarySubdivide(struct Bezier* bezier, double aX, double aA, double aB);
