#include <u.h>
#include <libc.h>
#include <geometry.h>

void
rframematrix(Matrix m, RFrame rf)
{
	m[0][0] = rf.bx.x; m[0][1] = rf.by.x; m[0][2] = rf.p.x;
	m[1][0] = rf.bx.y; m[1][1] = rf.by.y; m[1][2] = rf.p.y;
	m[2][0] = 0; m[2][1] = 0; m[2][2] = 1;
}

void
rframematrix3(Matrix3 m, RFrame3 rf)
{
	m[0][0] = rf.bx.x; m[0][1] = rf.by.x; m[0][2] = rf.bz.x; m[0][3] = rf.p.x;
	m[1][0] = rf.bx.y; m[1][1] = rf.by.y; m[1][2] = rf.bz.y; m[1][3] = rf.p.y;
	m[2][0] = rf.bx.z; m[2][1] = rf.by.z; m[2][2] = rf.bz.z; m[2][3] = rf.p.z;
	m[3][0] = 0; m[3][1] = 0; m[3][2] = 0; m[3][3] = 1;
}

Point2
rframexform(Point2 p, RFrame rf)
{
	Matrix m;

	rframematrix(m, rf);
	invm(m);
	return xform(p, m);
}

Point3
rframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m;

	rframematrix3(m, rf);
	invm3(m);
	return xform3(p, m);
}

Point2
invrframexform(Point2 p, RFrame rf)
{
	Matrix m;

	rframematrix(m, rf);
	return xform(p, m);
}

Point3
invrframexform3(Point3 p, RFrame3 rf)
{
	Matrix3 m;

	rframematrix3(m, rf);
	return xform3(p, m);
}
