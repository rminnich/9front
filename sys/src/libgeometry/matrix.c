#include <u.h>
#include <libc.h>
#include <geometry.h>

/* 2D */

void
identity(Matrix m)
{
	memset(m, 0, sizeof(Matrix));
	m[0][0] = m[1][1] = m[2][2] = 1;
}

void
addm(Matrix a, Matrix b)
{
	int i, j;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++)
			a[i][j] += b[i][j];
}

void
subm(Matrix a, Matrix b)
{
	int i, j;

	for(i = 0; i < 3; i++)
		for(j = 0; j < 3; j++)
			a[i][j] -= b[i][j];
}

void
mulm(Matrix a, Matrix b)
{
	double t0, t1, t2;

	t0 = a[0][0]; t1 = a[0][1]; t2 = a[0][2];
	a[0][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0];
	a[0][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1];
	a[0][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2];

	t0 = a[1][0]; t1 = a[1][1]; t2 = a[1][2];
	a[1][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0];
	a[1][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1];
	a[1][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2];

	t0 = a[2][0]; t1 = a[2][1]; t2 = a[2][2];
	a[2][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0];
	a[2][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1];
	a[2][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2];
}

void
smulm(Matrix m, double s)
{
	m[0][0] *= s; m[0][1] *= s; m[0][2] *= s;
	m[1][0] *= s; m[1][1] *= s; m[1][2] *= s;
	m[2][0] *= s; m[2][1] *= s; m[2][2] *= s;
}

void
transposem(Matrix m)
{
	int i, j;
	double tmp;

	for(i = 0; i < 3; i++)
		for(j = i; j < 3; j++){
			tmp = m[i][j];
			m[i][j] = m[j][i];
			m[j][i] = tmp;
		}
}

double
detm(Matrix m)
{
	return m[0][0]*(m[1][1]*m[2][2] - m[1][2]*m[2][1])+
	       m[0][1]*(m[1][2]*m[2][0] - m[1][0]*m[2][2])+
	       m[0][2]*(m[1][0]*m[2][1] - m[1][1]*m[2][0]);
}

double
tracem(Matrix m)
{
	return m[0][0] + m[1][1] + m[2][2];
}

double
minorm(Matrix m, int row, int col)
{
	int i, j;
	double subm[2][2];

	for(i = 0; i < 3-1; i++)
		for(j = 0; j < 3-1; j++)
			subm[i][j] = m[i < row? i: i+1][j < col? j: j+1];
	return subm[0][0]*subm[1][1] - subm[0][1]*subm[1][0];
}

double
cofactorm(Matrix m, int row, int col)
{
	return minorm(m, row, col)*((row+col & 1) == 0? 1: -1);
}

void
adjm(Matrix m)
{
	Matrix tmp;

	tmp[0][0] =  m[1][1]*m[2][2] - m[1][2]*m[2][1];
	tmp[0][1] = -m[0][1]*m[2][2] + m[0][2]*m[2][1];
	tmp[0][2] =  m[0][1]*m[1][2] - m[0][2]*m[1][1];

	tmp[1][0] = -m[1][0]*m[2][2] + m[1][2]*m[2][0];
	tmp[1][1] =  m[0][0]*m[2][2] - m[0][2]*m[2][0];
	tmp[1][2] = -m[0][0]*m[1][2] + m[0][2]*m[1][0];

	tmp[2][0] =  m[1][0]*m[2][1] - m[1][1]*m[2][0];
	tmp[2][1] = -m[0][0]*m[2][1] + m[0][1]*m[2][0];
	tmp[2][2] =  m[0][0]*m[1][1] - m[0][1]*m[1][0];

	memmove(m, tmp, sizeof tmp);
}

/* Cramer's */
void
invm(Matrix m)
{
	double det;

	det = detm(m);
	if(det == 0)
		return; /* singular matrices are not invertible */
	adjm(m);
	smulm(m, 1/det);
}

Point2
xform(Point2 p, Matrix m)
{
	return (Point2){
		p.x*m[0][0] + p.y*m[0][1] + p.w*m[0][2],
		p.x*m[1][0] + p.y*m[1][1] + p.w*m[1][2],
		p.x*m[2][0] + p.y*m[2][1] + p.w*m[2][2]
	};
}

/* 3D */

void
identity3(Matrix3 m)
{
	memset(m, 0, sizeof(Matrix3));
	m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1;
}

void
addm3(Matrix3 a, Matrix3 b)
{
	int i, j;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			a[i][j] += b[i][j];
}

void
subm3(Matrix3 a, Matrix3 b)
{
	int i, j;

	for(i = 0; i < 4; i++)
		for(j = 0; j < 4; j++)
			a[i][j] -= b[i][j];
}

void
mulm3(Matrix3 a, Matrix3 b)
{
	double t0, t1, t2, t3;

	t0 = a[0][0]; t1 = a[0][1]; t2 = a[0][2]; t3 = a[0][3];
	a[0][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0] + t3*b[3][0];
	a[0][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1] + t3*b[3][1];
	a[0][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2] + t3*b[3][2];
	a[0][3] = t0*b[0][3] + t1*b[1][3] + t2*b[2][3] + t3*b[3][3];

	t0 = a[1][0]; t1 = a[1][1]; t2 = a[1][2]; t3 = a[1][3];
	a[1][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0] + t3*b[3][0];
	a[1][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1] + t3*b[3][1];
	a[1][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2] + t3*b[3][2];
	a[1][3] = t0*b[0][3] + t1*b[1][3] + t2*b[2][3] + t3*b[3][3];

	t0 = a[2][0]; t1 = a[2][1]; t2 = a[2][2]; t3 = a[2][3];
	a[2][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0] + t3*b[3][0];
	a[2][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1] + t3*b[3][1];
	a[2][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2] + t3*b[3][2];
	a[2][3] = t0*b[0][3] + t1*b[1][3] + t2*b[2][3] + t3*b[3][3];

	t0 = a[3][0]; t1 = a[3][1]; t2 = a[3][2]; t3 = a[3][3];
	a[3][0] = t0*b[0][0] + t1*b[1][0] + t2*b[2][0] + t3*b[3][0];
	a[3][1] = t0*b[0][1] + t1*b[1][1] + t2*b[2][1] + t3*b[3][1];
	a[3][2] = t0*b[0][2] + t1*b[1][2] + t2*b[2][2] + t3*b[3][2];
	a[3][3] = t0*b[0][3] + t1*b[1][3] + t2*b[2][3] + t3*b[3][3];
}

void
smulm3(Matrix3 m, double s)
{
	m[0][0] *= s; m[0][1] *= s; m[0][2] *= s; m[0][3] *= s;
	m[1][0] *= s; m[1][1] *= s; m[1][2] *= s; m[1][3] *= s;
	m[2][0] *= s; m[2][1] *= s; m[2][2] *= s; m[2][3] *= s;
	m[3][0] *= s; m[3][1] *= s; m[3][2] *= s; m[3][3] *= s;
}

void
transposem3(Matrix3 m)
{
	int i, j;
	double tmp;

	for(i = 0; i < 4; i++)
		for(j = i; j < 4; j++){
			tmp = m[i][j];
			m[i][j] = m[j][i];
			m[j][i] = tmp;
		}
}

/*
 * extracted from invm3(2)
 */
double
detm3(Matrix3 m)
{
	double s0, s1, s2, s3, s4, s5;
	double c0, c1, c2, c3, c4, c5;

	s0 = m[0][0]*m[1][1] - m[1][0]*m[0][1];
	s1 = m[0][0]*m[1][2] - m[1][0]*m[0][2];
	s2 = m[0][0]*m[1][3] - m[1][0]*m[0][3];
	s3 = m[0][1]*m[1][2] - m[1][1]*m[0][2];
	s4 = m[0][1]*m[1][3] - m[1][1]*m[0][3];
	s5 = m[0][2]*m[1][3] - m[1][2]*m[0][3];

	c5 = m[2][2]*m[3][3] - m[3][2]*m[2][3];
	c4 = m[2][1]*m[3][3] - m[3][1]*m[2][3];
	c3 = m[2][1]*m[3][2] - m[3][1]*m[2][2];
	c2 = m[2][0]*m[3][3] - m[3][0]*m[2][3];
	c1 = m[2][0]*m[3][2] - m[3][0]*m[2][2];
	c0 = m[2][0]*m[3][1] - m[3][0]*m[2][1];

	return s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0;
}

double
tracem3(Matrix3 m)
{
	return m[0][0] + m[1][1] + m[2][2] + m[3][3];
}

double
minorm3(Matrix3 m, int row, int col)
{
	int i, j;
	Matrix subm;

	memset(subm, 0, sizeof subm);
	for(i = 0; i < 4-1; i++)
		for(j = 0; j < 4-1; j++)
			subm[i][j] = m[i < row? i: i+1][j < col? j: j+1];
	return detm(subm);
}

double
cofactorm3(Matrix3 m, int row, int col)
{
	return minorm3(m, row, col)*((row+col & 1) == 0? 1: -1);
}

/*
 * extracted from invm3(2)
 */
void
adjm3(Matrix3 m)
{
	double s0, s1, s2, s3, s4, s5;
	double c0, c1, c2, c3, c4, c5;
	Matrix3 tmp;

	s0 = m[0][0]*m[1][1] - m[1][0]*m[0][1];
	s1 = m[0][0]*m[1][2] - m[1][0]*m[0][2];
	s2 = m[0][0]*m[1][3] - m[1][0]*m[0][3];
	s3 = m[0][1]*m[1][2] - m[1][1]*m[0][2];
	s4 = m[0][1]*m[1][3] - m[1][1]*m[0][3];
	s5 = m[0][2]*m[1][3] - m[1][2]*m[0][3];

	c5 = m[2][2]*m[3][3] - m[3][2]*m[2][3];
	c4 = m[2][1]*m[3][3] - m[3][1]*m[2][3];
	c3 = m[2][1]*m[3][2] - m[3][1]*m[2][2];
	c2 = m[2][0]*m[3][3] - m[3][0]*m[2][3];
	c1 = m[2][0]*m[3][2] - m[3][0]*m[2][2];
	c0 = m[2][0]*m[3][1] - m[3][0]*m[2][1];

	tmp[0][0] = ( m[1][1]*c5 - m[1][2]*c4 + m[1][3]*c3);
	tmp[0][1] = (-m[0][1]*c5 + m[0][2]*c4 - m[0][3]*c3);
	tmp[0][2] = ( m[3][1]*s5 - m[3][2]*s4 + m[3][3]*s3);
	tmp[0][3] = (-m[2][1]*s5 + m[2][2]*s4 - m[2][3]*s3);

	tmp[1][0] = (-m[1][0]*c5 + m[1][2]*c2 - m[1][3]*c1);
	tmp[1][1] = ( m[0][0]*c5 - m[0][2]*c2 + m[0][3]*c1);
	tmp[1][2] = (-m[3][0]*s5 + m[3][2]*s2 - m[3][3]*s1);
	tmp[1][3] = ( m[2][0]*s5 - m[2][2]*s2 + m[2][3]*s1);

	tmp[2][0] = ( m[1][0]*c4 - m[1][1]*c2 + m[1][3]*c0);
	tmp[2][1] = (-m[0][0]*c4 + m[0][1]*c2 - m[0][3]*c0);
	tmp[2][2] = ( m[3][0]*s4 - m[3][1]*s2 + m[3][3]*s0);
	tmp[2][3] = (-m[2][0]*s4 + m[2][1]*s2 - m[2][3]*s0);

	tmp[3][0] = (-m[1][0]*c3 + m[1][1]*c1 - m[1][2]*c0);
	tmp[3][1] = ( m[0][0]*c3 - m[0][1]*c1 + m[0][2]*c0);
	tmp[3][2] = (-m[3][0]*s3 + m[3][1]*s1 - m[3][2]*s0);
	tmp[3][3] = ( m[2][0]*s3 - m[2][1]*s1 + m[2][2]*s0);

	memmove(m, tmp, sizeof tmp);
}

/*
 * David Eberly, “The Laplace Expansion Theorem: Computing the Determinants and Inverses of Matrices”, June 2024, p. 9
 */
void
invm3(Matrix3 m)
{
	double s0, s1, s2, s3, s4, s5;
	double c0, c1, c2, c3, c4, c5;
	double Δ;
	Matrix3 tmp;

	s0 = m[0][0]*m[1][1] - m[1][0]*m[0][1];
	s1 = m[0][0]*m[1][2] - m[1][0]*m[0][2];
	s2 = m[0][0]*m[1][3] - m[1][0]*m[0][3];
	s3 = m[0][1]*m[1][2] - m[1][1]*m[0][2];
	s4 = m[0][1]*m[1][3] - m[1][1]*m[0][3];
	s5 = m[0][2]*m[1][3] - m[1][2]*m[0][3];

	c5 = m[2][2]*m[3][3] - m[3][2]*m[2][3];
	c4 = m[2][1]*m[3][3] - m[3][1]*m[2][3];
	c3 = m[2][1]*m[3][2] - m[3][1]*m[2][2];
	c2 = m[2][0]*m[3][3] - m[3][0]*m[2][3];
	c1 = m[2][0]*m[3][2] - m[3][0]*m[2][2];
	c0 = m[2][0]*m[3][1] - m[3][0]*m[2][1];

	Δ = s0*c5 - s1*c4 + s2*c3 + s3*c2 - s4*c1 + s5*c0;
	if(Δ == 0)
		return;
	Δ = 1/Δ;

	tmp[0][0] = ( m[1][1]*c5 - m[1][2]*c4 + m[1][3]*c3)*Δ;
	tmp[0][1] = (-m[0][1]*c5 + m[0][2]*c4 - m[0][3]*c3)*Δ;
	tmp[0][2] = ( m[3][1]*s5 - m[3][2]*s4 + m[3][3]*s3)*Δ;
	tmp[0][3] = (-m[2][1]*s5 + m[2][2]*s4 - m[2][3]*s3)*Δ;

	tmp[1][0] = (-m[1][0]*c5 + m[1][2]*c2 - m[1][3]*c1)*Δ;
	tmp[1][1] = ( m[0][0]*c5 - m[0][2]*c2 + m[0][3]*c1)*Δ;
	tmp[1][2] = (-m[3][0]*s5 + m[3][2]*s2 - m[3][3]*s1)*Δ;
	tmp[1][3] = ( m[2][0]*s5 - m[2][2]*s2 + m[2][3]*s1)*Δ;

	tmp[2][0] = ( m[1][0]*c4 - m[1][1]*c2 + m[1][3]*c0)*Δ;
	tmp[2][1] = (-m[0][0]*c4 + m[0][1]*c2 - m[0][3]*c0)*Δ;
	tmp[2][2] = ( m[3][0]*s4 - m[3][1]*s2 + m[3][3]*s0)*Δ;
	tmp[2][3] = (-m[2][0]*s4 + m[2][1]*s2 - m[2][3]*s0)*Δ;

	tmp[3][0] = (-m[1][0]*c3 + m[1][1]*c1 - m[1][2]*c0)*Δ;
	tmp[3][1] = ( m[0][0]*c3 - m[0][1]*c1 + m[0][2]*c0)*Δ;
	tmp[3][2] = (-m[3][0]*s3 + m[3][1]*s1 - m[3][2]*s0)*Δ;
	tmp[3][3] = ( m[2][0]*s3 - m[2][1]*s1 + m[2][2]*s0)*Δ;

	memmove(m, tmp, sizeof tmp);
}

Point3
xform3(Point3 p, Matrix3 m)
{
	return (Point3){
		p.x*m[0][0] + p.y*m[0][1] + p.z*m[0][2] + p.w*m[0][3],
		p.x*m[1][0] + p.y*m[1][1] + p.z*m[1][2] + p.w*m[1][3],
		p.x*m[2][0] + p.y*m[2][1] + p.z*m[2][2] + p.w*m[2][3],
		p.x*m[3][0] + p.y*m[3][1] + p.z*m[3][2] + p.w*m[3][3],
	};
}
