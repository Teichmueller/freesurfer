#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>

#include "fio.h"
#include "const.h"
#include "diag.h"
#include "proto.h"
#include "macros.h"
#include "error.h"
//#include "MRIio.h"
#include "mri.h"
#include "mrisurf.h"
#include "matrix.h"
#include "stats.h"
#include "timer.h"
#include "const.h"
#include "mrishash.h"
#include "icosahedron.h"
#include "tritri.h"
#include "timer.h"
#include "chklc.h"
#include "mrisutils.h"
#include "cma.h"
#include "gca.h"
#include "sig.h"


///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
////                    USEFULL ROUTINES               ////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////

//smooth a surface 'niter' times with a step (should be around 0.5)
void MRISsmoothSurface(MRI_SURFACE *mris,int niter,float step)
{
  VERTEX *v;
  int iter,k,m,n;
  float x,y,z;  

  if(step>1)
    step=1.0f;

  for (iter=0;iter<niter;iter++)
  {
    MRIScomputeMetricProperties(mris) ;
    for (k=0;k<mris->nvertices;k++)
    {
      v = &mris->vertices[k];
      v->tx = v->x;
      v->ty = v->y;
      v->tz = v->z;
    }

    for (k=0;k<mris->nvertices;k++)
    {
      v = &mris->vertices[k];
      n=0;
      x = y = z = 0;
      for (m=0;m<v->vnum;m++)
      {
        x += mris->vertices[v->v[m]].tx;
        y += mris->vertices[v->v[m]].ty;
        z += mris->vertices[v->v[m]].tz;
  n++;
      }
      x/=n;
      y/=n;
      z/=n;

      v->x=v->x+step*(x-v->x);  
      v->y=v->y+step*(y-v->y);  
      v->z=v->z+step*(z-v->z);  
    }
  }
}



void MRIScenterCOG(MRIS* mris)
{
  MRIScenterCOG2(mris,NULL,NULL,NULL);
}

// translate the COG of a surface to (0,0,0)
void MRIScenterCOG2(MRI_SURFACE *mris,double *xCOG,double *yCOG,double *zCOG)
{
  int k;
  double x,y,z;
  x=0;
  y=0;
  z=0;
  for(k=0;k<mris->nvertices;k++)
  {
    x+=mris->vertices[k].x;
    y+=mris->vertices[k].y;
    z+=mris->vertices[k].z;
  }
  x/=mris->nvertices;
  y/=mris->nvertices;
  z/=mris->nvertices;
  for(k=0;k<mris->nvertices;k++)
  {
    mris->vertices[k].x-=x;
    mris->vertices[k].y-=y;
    mris->vertices[k].z-=z;
  }
  if(xCOG && yCOG && zCOG)
    {
      (*xCOG)=x;
      (*yCOG)=y;
      (*zCOG)=z;
    }
  /*       fprintf(stderr,"\nCOG Centered at x=%f y=%f z=%f",
     (float)x,(float)y,(float)z);*/
}

//peel a volume from a surface (World coordinates)
// *** type == 0:  keep the inside
//     type == 1:  peel the outside surface and set the inside value to 'val'
//     type == 2: keep the outside
//     type == 3: peel the inside surface and set the outside value to 'val'
// *** NbVoxels corresponds to:
//               - the number of kept voxels if (type>=0)
//               - the number of removed voxels if (type<0)
MRI* MRISpeelVolume(MRIS *mris,MRI *mri_src,MRI *mri_dst,int type,unsigned char val,unsigned long *NbVoxels)
{
  int i,j,k,imnr; 
  float x0,y0,z0,x1,y1,z1,x2,y2,z2,d0,d1,d2,dmax,u,v;
  float px,py,pz,px0,py0,pz0,px1,py1,pz1;
  int numu,numv,totalfilled,newfilled;
  double tx,ty,tz;
  unsigned char tmpval;
  unsigned long size;
  int width, height,depth;
  MRI *mri_buff;

  
  
  width=mri_src->width;
  height=mri_src->height;
  depth=mri_src->depth;

  if(mri_dst==NULL)
    mri_dst=MRIalloc(width,height,depth,mri_src->type);


  mri_buff= MRIalloc(width, height, depth, MRI_UCHAR) ;


  for (k=0;k<mris->nfaces;k++)
  {
    x0 =mris->vertices[mris->faces[k].v[0]].x;    
    y0 =mris->vertices[mris->faces[k].v[0]].y;    
    z0 =mris->vertices[mris->faces[k].v[0]].z;    
    x1 =mris->vertices[mris->faces[k].v[1]].x;    
    y1 =mris->vertices[mris->faces[k].v[1]].y;    
    z1 =mris->vertices[mris->faces[k].v[1]].z;    
    x2 =mris->vertices[mris->faces[k].v[2]].x;    
    y2 =mris->vertices[mris->faces[k].v[2]].y;    
    z2 =mris->vertices[mris->faces[k].v[2]].z;    
    d0 = sqrt(SQR(x1-x0)+SQR(y1-y0)+SQR(z1-z0));
    d1 = sqrt(SQR(x2-x1)+SQR(y2-y1)+SQR(z2-z1));
    d2 = sqrt(SQR(x0-x2)+SQR(y0-y2)+SQR(z0-z2));
    dmax = (d0>=d1&&d0>=d2)?d0:(d1>=d0&&d1>=d2)?d1:d2;
    numu = ceil(2*d0);
    numv = ceil(2*dmax);

      
    for (v=0;v<=numv;v++)
    {
      px0 = x0 + (x2-x0)*v/numv;
      py0 = y0 + (y2-y0)*v/numv;
      pz0 = z0 + (z2-z0)*v/numv;
      px1 = x1 + (x2-x1)*v/numv;
      py1 = y1 + (y2-y1)*v/numv;
      pz1 = z1 + (z2-z1)*v/numv;
      for (u=0;u<=numu;u++)
      {
        px = px0 + (px1-px0)*u/numu;
        py = py0 + (py1-py0)*u/numu;
        pz = pz0 + (pz1-pz0)*u/numu;

	// MRIworldToVoxel(mri_src,px,py,pz,&tx,&ty,&tz);
	MRIsurfaceRASToVoxel(mri_src,px,py,pz,&tx,&ty,&tz);
	imnr=(int)(tz+0.5);
	j=(int)(ty+0.5);
	i=(int)(tx+0.5);
	

	if (i>=0 && i<width && j>=0 && j<height && imnr>=0 && imnr<depth)
	  MRIvox(mri_buff,i,j,imnr) = 255;

      }  
    }
  }
 
  MRIvox(mri_buff,1,1,1)= 64;
  totalfilled = newfilled = 1;
  while (newfilled>0)
  {
    newfilled = 0;
    for (k=1;k<depth-1;k++)
      for (j=1;j<height-1;j++)
        for (i=1;i<width-1;i++)
          if (MRIvox(mri_buff,i,j,k)==0)
            if (MRIvox(mri_buff,i,j,k-1)==64||MRIvox(mri_buff,i,j-1,k)==64||
                MRIvox(mri_buff,i-1,j,k)==64)
            {
              MRIvox(mri_buff,i,j,k)= 64;
              newfilled++;
            }
    for (k=depth-2;k>=1;k--)
      for (j=height-2;j>=1;j--)
        for (i=width-2;i>=1;i--)
          if (MRIvox(mri_buff,i,j,k)==0)
            if (MRIvox(mri_buff,i,j,k+1)==64||MRIvox(mri_buff,i,j+1,k)==64||
                MRIvox(mri_buff,i+1,j,k)==64)
        {
    MRIvox(mri_buff,i,j,k) = 64;
    newfilled++;
        }
    totalfilled += newfilled;
  }

  size=0;
  switch(type)
    {
    case 0:      
      for (k=1;k<depth-1;k++)
	for (j=1;j<height-1;j++)
	  for (i=1;i<width-1;i++)
	    {
	      if (MRIvox(mri_buff,i,j,k)==64)
		MRIvox(mri_dst,i,j,k) = 0 ;
	      else
		{
		  tmpval=MRIvox(mri_src,i,j,k);
		  MRIvox(mri_dst,i,j,k) = tmpval;
		  size++;
		}
	    }
      break;
    case 1:
      for (k=1;k<depth-1;k++)
	for (j=1;j<height-1;j++)
	  for (i=1;i<width-1;i++)
	    {
	      if (MRIvox(mri_buff,i,j,k)==64)
		MRIvox(mri_dst,i,j,k) = 0 ;
	      else
		{
		  MRIvox(mri_dst,i,j,k) = val;
		  size++;
		}
	    }
      break;
    case 2:
      for (k=1;k<depth-1;k++)
	for (j=1;j<height-1;j++)
	  for (i=1;i<width-1;i++)
	    {
	      if (MRIvox(mri_buff,i,j,k)==64)
		{
		  tmpval=MRIvox(mri_src,i,j,k);
		  MRIvox(mri_dst,i,j,k) =  tmpval ;
		}
	      else
		{
		  MRIvox(mri_dst,i,j,k) = 0;
		  size++;
		}
	    }
      break;
    case 3:
      for (k=1;k<depth-1;k++)
	for (j=1;j<height-1;j++)
	  for (i=1;i<width-1;i++)
	    {
	      if (MRIvox(mri_buff,i,j,k)==64)
		MRIvox(mri_dst,i,j,k) =  val;
	      else
		{
		  MRIvox(mri_dst,i,j,k) = 0;
		  size++;
		}
	    }
      break;
    }
  if(NbVoxels)
    (*NbVoxels)=size;
  
  MRIfree(&mri_buff);
  return mri_dst;
}

///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
////   ROUTINES FOR MATCHING A SURFACE TO A VOLUME LABEL //////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////


static int mrisClearMomentum(MRI_SURFACE *mris);
static int mrisClearGradient(MRI_SURFACE *mris);
//static int mrisClearExtraGradient(MRI_SURFACE *mris);
static MRI* mriIsolateLabel(MRI* mri_seg,int label,MRI_REGION* bbox);
static int mrisAverageSignedGradients(MRI_SURFACE *mris, int num_avgs);
static double mrisRmsValError(MRI_SURFACE *mris, MRI *mri);
static void mrisSetVal(MRIS *mris,float val);
static double mrisAsynchronousTimeStepNew(MRI_SURFACE *mris, float momentum, 
				       float delta_t, MHT *mht, float max_mag);
static int mrisLimitGradientDistance(MRI_SURFACE *mris, MHT *mht, int vno);
static int mrisRemoveNeighborGradientComponent(MRI_SURFACE *mris, int vno);
static int mrisRemoveNormalGradientComponent(MRI_SURFACE *mris, int vno);
static int mrisComputeQuadraticCurvatureTerm(MRI_SURFACE *mris, double l_curv);
static int mrisComputeTangentPlanes(MRI_SURFACE *mris);
static int mrisComputeIntensityTerm(MRI_SURFACE *mris
				    , double l_intensity, MRI *mri,double sigma);
static int mrisComputeTangentialSpringTerm(MRI_SURFACE *mris, double l_spring);
static int mrisComputeNormalSpringTerm(MRI_SURFACE *mris, double l_spring);

static int
mrisClearMomentum(MRI_SURFACE *mris)
{
  int     vno, nvertices ;
  VERTEX  *v ;

  nvertices = mris->nvertices ;
  for (vno = 0 ; vno < nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    v->odx = 0 ; v->ody = 0 ; v->odz = 0 ;
  }
  return(NO_ERROR) ;
}

static int
mrisClearGradient(MRI_SURFACE *mris)
{
  int     vno, nvertices ;
  VERTEX  *v ;

  nvertices = mris->nvertices ;
  for (vno = 0 ; vno < nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    v->dx = 0 ; v->dy = 0 ; v->dz = 0 ;
  }
  return(NO_ERROR) ;
}
/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
------------------------------------------------------*/
#if 0
static int
mrisClearExtraGradient(MRI_SURFACE *mris)
{
  int     vno, nvertices ;
  VERTEX  *v ;

  nvertices = mris->nvertices ;
  for (vno = 0 ; vno < nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    if (mris->dx2)
      mris->dx2[vno] = mris->dy2[vno] = mris->dz2[vno] = 0 ;
  }
  return(NO_ERROR) ;
}
#endif

static int
mrisAverageSignedGradients(MRI_SURFACE *mris, int num_avgs)
{
  int    i, vno, vnb, *pnb, vnum ;
  float  dx, dy, dz, num, sigma, dot ;
  VERTEX *v, *vn ;
  MRI_SP *mrisp, *mrisp_blur ;

  if (num_avgs <= 0)
    return(NO_ERROR) ;

  if (Gdiag_no >= 0)
  {
    v = &mris->vertices[Gdiag_no] ;
    fprintf(stdout, "before averaging dot = %2.2f ",
            v->dx*v->nx+v->dy*v->ny+v->dz*v->nz) ;
  }
  if (0 && mris->status == MRIS_PARAMETERIZED_SPHERE)  /* use convolution */
  {
    sigma = sqrt((float)num_avgs) / 4.0 ;
    mrisp = MRISgradientToParameterization(mris, NULL, 1.0) ;
    mrisp_blur = MRISPblur(mrisp, NULL, sigma, -1) ;
    MRISgradientFromParameterization(mrisp_blur, mris) ;
    MRISPfree(&mrisp) ; MRISPfree(&mrisp_blur) ;
  }
  else for (i = 0 ; i < num_avgs ; i++)
  {
    for (vno = 0 ; vno < mris->nvertices ; vno++)
    {
      v = &mris->vertices[vno] ;
      if (v->ripflag)
        continue ;
      dx = v->dx ; dy = v->dy ; dz = v->dz ;
      pnb = v->v ;
      /*      vnum = v->v2num ? v->v2num : v->vnum ;*/
      vnum = v->vnum ;
      for (num = 0.0f, vnb = 0 ; vnb < vnum ; vnb++)
      {
        vn = &mris->vertices[*pnb++] ;    /* neighboring vertex pointer */
        if (vn->ripflag)
          continue ;
				dot = vn->dx * v->dx + vn->dy * v->dy + vn->dz*v->dz ;
				if (dot < 0)
					continue ;  /* pointing in opposite directions */

        num++ ;
        dx += vn->dx ; dy += vn->dy ; dz += vn->dz ;
#if 0
        if (vno == Gdiag_no)
        {
          float dot ;
          dot = vn->dx*v->dx + vn->dy*v->dy + vn->dz*v->dz ;
          if (dot < 0)
            fprintf(stdout, "vn %d: dot = %2.3f, dx = (%2.3f, %2.3f, %2.3f)\n",
                    v->v[vnb], dot, vn->dx, vn->dy, vn->dz) ;
        }
#endif
      }
      num++ ;
      v->tdx = dx / num ;
      v->tdy = dy / num ;
      v->tdz = dz / num ;
    }
    for (vno = 0 ; vno < mris->nvertices ; vno++)
    {
      v = &mris->vertices[vno] ;
      if (v->ripflag)
        continue ;
      v->dx = v->tdx ; v->dy = v->tdy ; v->dz = v->tdz ;
    }
  }
  if (Gdiag_no >= 0)
  {
    float dot ;
    v = &mris->vertices[Gdiag_no] ;
    dot = v->nx*v->dx + v->ny*v->dy + v->nz*v->dz ;
    fprintf(stdout, " after dot = %2.2f\n",dot) ;
    if (fabs(dot) > 50)
      DiagBreak() ;
  }
  return(NO_ERROR) ;
}


MRI_REGION* MRIlocateRegion(MRI *mri,int label)
{
  int i,j,k;
  int xmin,xmax,ymin,ymax,zmin,zmax;
  MRI_REGION *mri_region=(MRI_REGION*)malloc(sizeof(MRI_REGION));

  zmax=ymax=xmax=0;
  zmin=ymin=xmin=10000;
  
  for (k=0;k<mri->depth;k++)
    for(j=0;j<mri->height;j++)
      for(i=0;i<mri->width;i++)
	if(MRIvox(mri,i,j,k)==label)
	  {
	    if(k<zmin)
	      zmin=k;
	    if(j<ymin)
	      ymin=j;
	    if(i<xmin)
	      xmin=i;
	    if(k>zmax)
	      zmax=k;
	    if(j>ymax)
	      ymax=j;
	    if(i>xmax)
	      xmax=i;
	  }

  mri_region->x=xmin;
  mri_region->y=ymin;
  mri_region->z=zmin;
  mri_region->dx=xmax-xmin;
  mri_region->dy=ymax-ymin;
  mri_region->dz=zmax-zmin;

  return mri_region;
}

static MRI* mriIsolateLabel(MRI* mri_seg,int label,MRI_REGION* bbox)
{
  int i,j,k;
  int xplusdx,yplusdy,zplusdz;
  MRI *mri=MRIalloc(mri_seg->width,mri_seg->height,mri_seg->depth,mri_seg->type);

  xplusdx=bbox->x+bbox->dx+1;
  yplusdy=bbox->y+bbox->dy+1;
  zplusdz=bbox->z+bbox->dz+1;
  
  for (k=bbox->z;k<zplusdz;k++)
    for(j=bbox->y;j<yplusdy;j++)
      for(i=bbox->x;i<xplusdx;i++)
	if(MRIvox(mri_seg,i,j,k)==label)
	  MRIvox(mri,i,j,k)=1;

  return mri;
}

static double
mrisRmsValError(MRI_SURFACE *mris, MRI *mri)
{
  int     vno, n, xv, yv, zv ;
  Real    val, total, delta, x, y, z ;
  VERTEX  *v ;

  for (total = 0.0, n = vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag || v->val < 0)
      continue ;
    n++ ;
    MRISvertexToVoxel(v, mri, &x, &y, &z) ;
    xv = nint(x) ; yv = nint(y) ; zv = nint(z) ;
    MRIsampleVolume(mri, x, y, z, &val) ;
    delta = (val - v->val) ;
    total += delta*delta ;
  }
  return(sqrt(total / (double)n)) ;
}

static void mrisSetVal(MRIS *mris,float val)
{
  int n;
  for(n=0;n<mris->nvertices;n++)
    mris->vertices[n].val=val;
}

#define MIN_NBR_DIST  (0.25)


static int
mrisRemoveNormalGradientComponent(MRI_SURFACE *mris, int vno)
{
  VERTEX   *v ;
  float    dot ;

  v = &mris->vertices[vno] ;
  if (v->ripflag)
    return(NO_ERROR) ;
  
  dot = v->nx*v->odx + v->ny*v->ody + v->nz*v->odz ;
  v->odx -= dot*v->nx ;
  v->ody -= dot*v->ny ;
  v->odz -= dot*v->nz ;

  return(NO_ERROR) ;
}

static int 
mrisRemoveNeighborGradientComponent(MRI_SURFACE *mris, int vno)
{
  VERTEX   *v, *vn ;
  int      n ;
  float    dx, dy, dz, dot, x, y, z, dist ;

  v = &mris->vertices[vno] ;
  if (v->ripflag)
    return(NO_ERROR) ;
  
  x = v->x ; y = v->y ; z = v->z ; 
  for (n = 0 ; n < v->vnum ; n++)
  {
    vn = &mris->vertices[v->v[n]] ;
    dx = vn->x - x ; dy = vn->y - y ; dz = vn->z - z ;
    dist = sqrt(dx*dx + dy*dy + dz*dz) ;

    /* too close - take out gradient component in this dir. */
    if (dist <= MIN_NBR_DIST)  
    {
      dx /= dist ; dy /= dist ; dz /= dist ;
      dot = dx*v->odx + dy*v->ody + dz*v->odz ;
      if (dot > 0.0)
      {
        v->odx -= dot*dx ;
        v->ody -= dot*dy ;
        v->odz -= dot*dz ;
      }
    }
  }

  return(NO_ERROR) ;
}


static int
mrisLimitGradientDistance(MRI_SURFACE *mris, MHT *mht, int vno)
{
  VERTEX   *v ;

  v = &mris->vertices[vno] ;

  mrisRemoveNeighborGradientComponent(mris, vno) ;
  if (MHTisVectorFilled(mht, mris, vno, v->odx, v->ody, v->odz))
  {
    mrisRemoveNormalGradientComponent(mris, vno) ;
    if (MHTisVectorFilled(mht, mris, vno, v->odx, v->ody, v->odz))
    {
      v->odx = v->ody = v->odz = 0.0 ;
      return(NO_ERROR) ;
    }
  }

  return(NO_ERROR) ;
}


static double  
mrisAsynchronousTimeStepNew(MRI_SURFACE *mris, float momentum, 
                            float delta_t, MHT *mht, float max_mag)
{
  static int direction = 1 ;
  double  mag ;
  int     vno, i ;
  VERTEX  *v ;

  for (i = 0 ; i < mris->nvertices ; i++)
  {
    if (direction < 0)
      vno = mris->nvertices - i - 1 ;
    else
      vno = i ;
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;
    v->odx = delta_t * v->dx + momentum*v->odx ;
    v->ody = delta_t * v->dy + momentum*v->ody ;
    v->odz = delta_t * v->dz + momentum*v->odz ;
    mag = sqrt(v->odx*v->odx + v->ody*v->ody + v->odz*v->odz) ;
    if (mag > max_mag) /* don't let step get too big */
    {
      mag = max_mag / mag ;
      v->odx *= mag ; v->ody *= mag ; v->odz *= mag ;
    }

    /* erase the faces this vertex is part of */

    if (mht)
      MHTremoveAllFaces(mht, mris, v) ;

    if (mht)
      mrisLimitGradientDistance(mris, mht, vno) ;

    v->x += v->odx ; 
    v->y += v->ody ;
    v->z += v->odz ;

    if ((fabs(v->x) > 128.0f) ||
        (fabs(v->y) > 128.0f) ||
        (fabs(v->z) > 128.0f))
      DiagBreak() ;

    if (mht)
      MHTaddAllFaces(mht, mris, v) ;
  }

  direction *= -1 ;
  return(delta_t) ;
}

#define VERTEX_EDGE(vec, v0, v1)   VECTOR_LOAD(vec,v1->x-v0->x,v1->y-v0->y,\
                                               v1->z-v0->z)

static int
mrisComputeTangentPlanes(MRI_SURFACE *mris)
{
  VECTOR  *v_n, *v_e1, *v_e2, *v ;
  int     vno ;
  VERTEX  *vertex ;

  v_n = VectorAlloc(3, MATRIX_REAL) ;
  v_e1 = VectorAlloc(3, MATRIX_REAL) ;
  v_e2 = VectorAlloc(3, MATRIX_REAL) ;
  v = VectorAlloc(3, MATRIX_REAL) ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    vertex = &mris->vertices[vno] ;
    if (vno == Gdiag_no)
      DiagBreak() ;
    VECTOR_LOAD(v_n, vertex->nx, vertex->ny, vertex->nz) ;
    /* now find some other non-parallel vector */
#if 0
    if (!FZERO(vertex->nx) || !FZERO(vertex->ny))
    {VECTOR_LOAD(v, 0.0, 0.0, 1.0) ; }
    else
    {VECTOR_LOAD(v, 0.0, 1.0, 0.0) ; }
#else
    VECTOR_LOAD(v, vertex->ny, vertex->nz, vertex->nx) ;
#endif
    V3_CROSS_PRODUCT(v_n, v, v_e1) ;
    if ((V3_LEN_IS_ZERO(v_e1)))  /* happened to pick a parallel vector */
    {
      VECTOR_LOAD(v, vertex->ny, -vertex->nz, vertex->nx) ;
      V3_CROSS_PRODUCT(v_n, v, v_e1) ;
    }

    if ((V3_LEN_IS_ZERO(v_e1)) && DIAG_VERBOSE_ON)  /* happened to pick a parallel vector */
      fprintf(stderr, "vertex %d: degenerate tangent plane\n", vno) ;
    V3_CROSS_PRODUCT(v_n, v_e1, v_e2) ;
    V3_NORMALIZE(v_e1, v_e1) ;
    V3_NORMALIZE(v_e2, v_e2) ;
    vertex->e1x = V3_X(v_e1) ; vertex->e2x = V3_X(v_e2) ;
    vertex->e1y = V3_Y(v_e1) ; vertex->e2y = V3_Y(v_e2) ;
    vertex->e1z = V3_Z(v_e1) ; vertex->e2z = V3_Z(v_e2) ;
  }

  VectorFree(&v) ;
  VectorFree(&v_n) ;
  VectorFree(&v_e1) ;
  VectorFree(&v_e2) ;
  return(NO_ERROR) ;
}

/*-----------------------------------------------------
        Parameters:

        Returns value:

        Description
          Fit a 1-d quadratic to the surface locally and move the
          vertex in the normal direction to improve the fit.
------------------------------------------------------*/
static int
mrisComputeQuadraticCurvatureTerm(MRI_SURFACE *mris, double l_curv)
{
  MATRIX   *m_R, *m_R_inv ;
  VECTOR   *v_Y, *v_A, *v_n, *v_e1, *v_e2, *v_nbr ;
  int      vno, n ;
  VERTEX   *v, *vn ;
  float    ui, vi, rsq, a, b ;

  if (FZERO(l_curv))
    return(NO_ERROR) ;

  mrisComputeTangentPlanes(mris) ;
  v_n = VectorAlloc(3, MATRIX_REAL) ;
  v_A = VectorAlloc(2, MATRIX_REAL) ;
  v_e1 = VectorAlloc(3, MATRIX_REAL) ;
  v_e2 = VectorAlloc(3, MATRIX_REAL) ;
  v_nbr = VectorAlloc(3, MATRIX_REAL) ;
  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    v_Y = VectorAlloc(v->vtotal, MATRIX_REAL) ;    /* heights above TpS */
    m_R = MatrixAlloc(v->vtotal, 2, MATRIX_REAL) ; /* radial distances */
    VECTOR_LOAD(v_n, v->nx, v->ny, v->nz) ;
    VECTOR_LOAD(v_e1, v->e1x, v->e1y, v->e1z) ;
    VECTOR_LOAD(v_e2, v->e2x, v->e2y, v->e2z) ;
    for (n = 0 ; n < v->vtotal ; n++)  /* build data matrices */
    {
      vn = &mris->vertices[v->v[n]] ;
      VERTEX_EDGE(v_nbr, v, vn) ;
      VECTOR_ELT(v_Y, n+1) = V3_DOT(v_nbr, v_n) ;
      ui = V3_DOT(v_e1, v_nbr) ; vi = V3_DOT(v_e2, v_nbr) ; 
      rsq = ui*ui + vi*vi ;
      *MATRIX_RELT(m_R, n+1, 1) = rsq ;
      *MATRIX_RELT(m_R, n+1, 2) = 1 ;
    }
    m_R_inv = MatrixPseudoInverse(m_R, NULL) ;
    if (!m_R_inv)
    {
      MatrixFree(&m_R) ; VectorFree(&v_Y) ;
      continue ;
    }
    v_A = MatrixMultiply(m_R_inv, v_Y, v_A) ;
    a = VECTOR_ELT(v_A, 1) ;
    b = VECTOR_ELT(v_A, 2) ; b *= l_curv ;
    v->dx += b * v->nx ; v->dy += b * v->ny ; v->dz += b * v->nz ; 

    if (vno == Gdiag_no)
      fprintf(stdout, "v %d curvature term:      (%2.3f, %2.3f, %2.3f), "
              "a=%2.2f, b=%2.1f\n",
              vno, b*v->nx, b*v->ny, b*v->nz, a, b) ;
    MatrixFree(&m_R) ; VectorFree(&v_Y) ; MatrixFree(&m_R_inv) ;
  }

  VectorFree(&v_n) ; VectorFree(&v_e1) ; VectorFree(&v_e2) ; 
  VectorFree(&v_nbr) ; VectorFree(&v_A) ;
  return(NO_ERROR) ;
}


static int
mrisComputeIntensityTerm(MRI_SURFACE *mris, double l_intensity, MRI *mri, double sigma)
{
  int     vno ;
  VERTEX  *v ;
  float   x, y, z, nx, ny, nz, dx, dy, dz ;
  Real    val0, xw, yw, zw, del, val_outside, val_inside, delI, delV;
  //int k,ktotal ;

  if (FZERO(l_intensity))
    return(NO_ERROR) ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag || v->val < 0)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;

    x = v->x ; y = v->y ; z = v->z ;

    // MRIworldToVoxel(mri, x, y, z, &xw, &yw, &zw) ;
    MRIsurfaceRASToVoxel(mri, x, y, z, &xw, &yw, &zw) ;
    MRIsampleVolume(mri, xw, yw, zw, &val0) ;
    //    sigma = v->val2 ;

    nx = v->nx ; ny = v->ny ; nz = v->nz ;

#if 1
    {
      Real val;
      xw = x + nx ; yw = y + ny ; zw = z + nz ;
      // MRIworldToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
      MRIsurfaceRASToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
      MRIsampleVolume(mri, xw, yw, zw, &val) ;
      val_outside = val ;

      
      
      xw = x - nx ; yw = y - ny ; zw = z - nz ;
      // MRIworldToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
      MRIsurfaceRASToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
      MRIsampleVolume(mri, xw, yw, zw, &val) ;
      val_inside = val ;
    }
#else
    /* compute intensity gradient using smoothed volume */
    {
      Real dist, val, step_size ;
      int  n ;

      step_size = MIN(sigma/2, 0.5) ;
      ktotal = 0.0 ;
      for (n = 0, val_outside = val_inside = 0.0, dist = step_size ; 
           dist <= 2*sigma; 
           dist += step_size, n++)
      {
        k = exp(-dist*dist/(2*sigma*sigma)) ; ktotal += k ;
        xw = x + dist*nx ; yw = y + dist*ny ; zw = z + dist*nz ;
        // MRIworldToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
        MRIsurfaceRASToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
        MRIsampleVolume(mri, xw, yw, zw, &val) ;
        val_outside += k*val ;

        xw = x - dist*nx ; yw = y - dist*ny ; zw = z - dist*nz ;
        // MRIworldToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
        MRIsurfaceRASToVoxel(mri, xw, yw, zw, &xw, &yw, &zw) ;
        MRIsampleVolume(mri, xw, yw, zw, &val) ;
        val_inside += k*val ;
      }
      val_inside /= (double)ktotal ; val_outside /= (double)ktotal ;
    }
#endif


    delV = v->val - val0 ;
    delI = (val_outside - val_inside) / 2.0 ;

    if (!FZERO(delI))
      delI /= fabs(delI) ;
    else if(delV<0) /*we are inside*/
      delI = -1 ;   
    else
      delI = 1 ;   /* intensities tend to increase inwards */
    
    del = l_intensity * delV * delI ;

    dx = nx * del ; dy = ny * del ; dz = nz * del ;      

    v->dx += dx ;   
    v->dy += dy ;
    v->dz += dz ;

  }
  
  return(NO_ERROR) ;
}

static int
mrisComputeNormalSpringTerm(MRI_SURFACE *mris, double l_spring)
{
  int     vno, n, m ;
  VERTEX  *vertex, *vn ;
  float   sx, sy, sz, nx, ny, nz, nc, x, y, z ;

  if (FZERO(l_spring))
    return(NO_ERROR) ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    vertex = &mris->vertices[vno] ;
    if (vertex->ripflag)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;

    nx = vertex->nx ; ny = vertex->ny ; nz = vertex->nz ;
    x = vertex->x ;    y = vertex->y ;   z = vertex->z ;

    sx = sy = sz = 0.0 ;
    n=0;
    for (m = 0 ; m < vertex->vnum ; m++)
    {
      vn = &mris->vertices[vertex->v[m]] ;
      if (!vn->ripflag)
      {
        sx += vn->x - x;
        sy += vn->y - y;
        sz += vn->z - z;
        n++;
      }
    }
    if (n>0)
    {
      sx = sx/n;
      sy = sy/n;
      sz = sz/n;
    }
    nc = sx*nx+sy*ny+sz*nz;   /* projection onto normal */
    sx = l_spring*nc*nx ;              /* move in normal direction */
    sy = l_spring*nc*ny ;
    sz = l_spring*nc*nz ;
    
    vertex->dx += sx ;
    vertex->dy += sy ;
    vertex->dz += sz ;
    if (vno == Gdiag_no)
      fprintf(stdout, "v %d spring normal term:  (%2.3f, %2.3f, %2.3f)\n",
              vno, sx, sy, sz) ;
  }
  

  return(NO_ERROR) ;
}

static int
mrisComputeTangentialSpringTerm(MRI_SURFACE *mris, double l_spring)
{
  int     vno, n, m ;
  VERTEX  *v, *vn ;
  float   sx, sy, sz, x, y, z, nc ;

  if (FZERO(l_spring))
    return(NO_ERROR) ;

  for (vno = 0 ; vno < mris->nvertices ; vno++)
  {
    v = &mris->vertices[vno] ;
    if (v->ripflag)
      continue ;
    if (vno == Gdiag_no)
      DiagBreak() ;

    if (v->border && !v->neg)
      continue ;

    x = v->x ;    y = v->y ;   z = v->z ;

    sx = sy = sz = 0.0 ;
    n=0;
    for (m = 0 ; m < v->vnum ; m++)
    {
      vn = &mris->vertices[v->v[m]] ;
      if (!vn->ripflag)
      {
        sx += vn->x - x;
        sy += vn->y - y;
        sz += vn->z - z;
        n++;
      }
    }
#if 0
    n = 4 ;  /* avg # of nearest neighbors */
#endif
    if (n>0)
    {
      sx = sx/n;
      sy = sy/n;
      sz = sz/n;
    }
    
    nc = sx*v->nx+sy*v->ny+sz*v->nz;   /* projection onto normal */
    sx -= l_spring*nc*v->nx ;                   /* remove  normal component */
    sy -= l_spring*nc*v->ny ;
    sz -= l_spring*nc*v->nz;

    v->dx += sx ;
    v->dy += sy ;
    v->dz += sz ;
    if (vno == Gdiag_no)
      fprintf(stdout, "v %d spring tangent term: (%2.3f, %2.3f, %2.3f)\n",
              vno, sx, sy, sz) ;
  }
  

  return(NO_ERROR) ;
}


#define MOMENTUM 0.8
#define MAX_ASYNCH_MM       0.3
#define MAX_REDUCTIONS     0
#define REDUCTION_PCT      0.5
#define AVERAGES_NBR 1
#define BASE_DT_SCALE    1.0

MRIS *MRISmatchSurfaceToLabel(MRIS *mris,MRI *mri_seg,int label,MRI_REGION *mri_region,INTEGRATION_PARMS *integration_parms,int connectivity)
{
  int bbox_indicator=0,done,niterations,n,nreductions=0;
  MRI_REGION *bbox;
  MRI *mri;
  INTEGRATION_PARMS *parms;
  int parms_indicator=0;
  double sse,last_sse,rms,last_rms,base_dt,dt,delta_t=0.0;
  double tol;
  MHT *mht = NULL;
  int avgs=AVERAGES_NBR;
  struct timeb then; int msec;


  TimerStart(&then);

  if(integration_parms==NULL)
    {
      parms=(INTEGRATION_PARMS*)calloc(1,sizeof(INTEGRATION_PARMS));
      parms_indicator=1;
      parms->projection = NO_PROJECTION ;
      parms->niterations=5;
      parms->dt=0.5f;
      parms->base_dt = BASE_DT_SCALE*parms->dt ;
      parms->tol=1e-3;
      parms->l_spring = 0.0f ; 
      parms->l_curv = 1.0 ; 
      parms->l_intensity = 1.0f ;
      parms->l_tspring = 1.0f ; 
      parms->l_nspring = 0.2f ;
      parms->momentum = MOMENTUM;
      parms->dt_increase = 1.0 /* DT_INCREASE */;
      parms->dt_decrease = 0.50 /* DT_DECREASE*/ ;
      parms->error_ratio = 50.0 /*ERROR_RATIO */;
      /*  parms.integration_type = INTEGRATE_LINE_MINIMIZE ;*/
      parms->l_surf_repulse = 1.0 ;
      parms->l_repulse = 0.0;// ;
      parms->sigma=2.0f;
      parms->mri_brain=mri_seg; /*necessary for using MRIcomputeSSE*/
    }
  else
      parms=integration_parms;
 
  niterations=parms->niterations;
  base_dt=parms->base_dt;
  tol=parms->tol;
    
  if(mri_region)
    bbox=mri_region;
  else
    {
      bbox=MRIlocateRegion(mri_seg,label);
      bbox_indicator=1;
    }

  mri=mriIsolateLabel(mri_seg,label,bbox);

  mrisClearMomentum(mris);
  MRIScomputeMetricProperties(mris);
  MRISstoreMetricProperties(mris);
  MRIScomputeNormals(mris);
  
  switch(connectivity)
    {
    case 1:
      mrisSetVal(mris,0.65);
      break;
    case 2:
      mrisSetVal(mris,0.5);
      break;
    case 3:
      mrisSetVal(mris,0.75);
      break;
    case 4:
      mrisSetVal(mris,0.3);
      break; 
    default:
      mrisSetVal(mris,0.5);
      break;
    }

  last_rms=rms=mrisRmsValError(mris,mri);

  mris->noscale=1;
  last_sse=sse=MRIScomputeSSE(mris,parms)/(double)mris->nvertices;


  if(1)//Gdiag & DIAG_SHOW)
    fprintf(stdout,"%3.3d: dt: %2.4f, sse=%2.4f, rms=%2.4f\n",
	    0,0.0f,(float)sse,(float)rms);

  for(n=0;n<niterations;n++)
    {
      dt=base_dt;
      nreductions=0;

      mht=MHTfillTable(mris,mht);

      mrisClearGradient(mris);
      MRISstoreMetricProperties(mris);
      MRIScomputeNormals(mris);

      /*intensity based terms*/
      mrisComputeIntensityTerm(mris,parms->l_intensity,mri, parms->sigma);

      mrisAverageSignedGradients(mris,avgs);

      /*smoothness terms*/
      mrisComputeQuadraticCurvatureTerm(mris,parms->l_curv);
      mrisComputeNormalSpringTerm(mris,parms->l_nspring);
      mrisComputeTangentialSpringTerm(mris,parms->l_tspring);
      
      //      sprintf(fname,"./zurf%d",n);
      //MRISwrite(mris,fname);

      do
	{
	  MRISsaveVertexPositions(mris,TMP_VERTICES);
	  delta_t=mrisAsynchronousTimeStepNew(mris,parms->momentum,dt,mht,MAX_ASYNCH_MM);

	  MRIScomputeMetricProperties(mris);
	  rms=mrisRmsValError(mris,mri);
	  sse=MRIScomputeSSE(mris,parms)/(double)mris->nvertices;
	  
	  done=1;
	  if(rms>last_rms-tol) /*error increased - reduce step size*/
	    {
	      nreductions++;
	      dt*=REDUCTION_PCT;
	      if(0)//Gdiag & DIAG_SHOW)
		fprintf(stdout,"      sse=%2.1f, last_sse=%2.1f,\n      ->  time setp reduction %d of %d to %2.3f...\n",
			sse,last_sse,nreductions,MAX_REDUCTIONS+1,dt);

	      mrisClearMomentum(mris);
	      if(rms>last_rms) /*error increased - reject step*/
		{
		  MRISrestoreVertexPositions(mris,TMP_VERTICES);
		  MRIScomputeMetricProperties(mris);
		  done=(nreductions>MAX_REDUCTIONS);
		}
	    }
	}while(!done);
      last_sse=sse;
      last_rms=rms;

      rms=mrisRmsValError(mris,mri);
      sse=MRIScomputeSSE(mris,parms)/(double)mris->nvertices;
      fprintf(stdout,"%3d, sse=%2.1f, last sse=%2.1f,\n     rms=%2.4f, last_rms=%2.4f\n",
	      n,sse,last_sse,rms,last_rms);
    }

  msec=TimerStop(&then);
  if(1)//Gdiag & DIAG_SHOW)
    fprintf(stdout,"positioning took %2.2f minutes\n",(float)msec/(60*1000.0f));


  if(bbox_indicator)
    free(bbox);
  if(parms_indicator)
    free(parms);
  MRIfree(&mri);
  MHTfree(&mht);

  return mris;
}



//smooth a surface 'niter' times with a step (should be around 0.5)
void MRISsmoothSurface2(MRI_SURFACE *mris,int niter,float step,int avrg)
{
  VERTEX *v;
  int iter,k,m,n;
  float x,y,z;  

  if(step>1)
    step=1.0f;

  for (iter=0;iter<niter;iter++)
  {
    MRIScomputeMetricProperties(mris) ;
    for (k=0;k<mris->nvertices;k++)
    {
      v = &mris->vertices[k];
      v->tx = v->x;
      v->ty = v->y;
      v->tz = v->z;
    }

    for (k=0;k<mris->nvertices;k++)
    {
      v = &mris->vertices[k];
      n=0;
      x = y = z = 0;
      for (m=0;m<v->vnum;m++)
      {
        x += mris->vertices[v->v[m]].tx;
        y += mris->vertices[v->v[m]].ty;
        z += mris->vertices[v->v[m]].tz;
  n++;
      }
      x/=n;
      y/=n;
      z/=n;

      v->dx=step*(x-v->x);
      v->dy=step*(y-v->y);
      v->dz=step*(z-v->z);

    }
    mrisAverageSignedGradients(mris,avrg);
    for (k=0;k<mris->nvertices;k++)
      {
	v = &mris->vertices[k];
	v->x+=v->dx;  
	v->y+=v->dy;  
	v->z+=v->dz; 
      } 
  }
}
/*--------------------------------------------------------------------*/
MRIS *MRISloadSurfSubject(char *subj, char *hemi, char *surfid, 
			  char *SUBJECTS_DIR)
{
  MRIS *Surf;
  char fname[2000];

  if(SUBJECTS_DIR == NULL){
    SUBJECTS_DIR = getenv("SUBJECTS_DIR");
    if(SUBJECTS_DIR==NULL){
      printf("ERROR: SUBJECTS_DIR not defined in environment.\n");
      return(NULL);
    }
  }

  sprintf(fname,"%s/%s/surf/%s.%s",SUBJECTS_DIR,subj,hemi,surfid);
  printf("  INFO: loading surface  %s\n",fname);
  fflush(stdout);
  Surf = MRISread(fname) ;
  if(Surf == NULL){
    printf("ERROR: could not load registration surface\n");
    exit(1);
  }
  printf("nvertices = %d\n",Surf->nvertices);fflush(stdout);

  return(Surf);
}
/*-----------------------------------------------------------------
  MRISfdr() - performs False Discovery Rate (FDR) thesholding of
  the the vertex val field. Results are stored in the val2 field.

  fdr - false dicovery rate, between 0 and 1, eg: .05
  signid -  
      0 = use all values regardless of sign
     +1 = use only positive values
     -1 = use only negative values
     If a vertex does not meet the sign criteria, its val2 is 0
  log10flag - interpret val field as -log10(p)
  maskflag - use the undefval field as a mask. If the undefval of
     a vertex is 1, then its val will be used to compute the threshold
     (if the val also meets the sign criteria). If the undefval is
     0, then val2 will be set to 0.
  fdrthresh - FDR threshold between 0 and 1. If log10flag is set,
     then fdrthresh = -log10(fdrthresh). Vertices with p values 
     GREATER than fdrthresh have val2=0. Note that this is the same
     as requiring -log10(p) > -log10(fdrthresh).

  So, for the val2 to be set to something non-zero, the val must
  meet the sign, mask, and threshold criteria. If val meets all
  the criteria, then val2=val (ie, no log10 transforms). The val
  field itself is not altered.

  Return Values:
    0 - evertying is OK
    1 - no vertices met the mask and sign criteria

  Ref: http://www.sph.umich.edu/~nichols/FDR/FDR.m
  ---------------------------------------------------------------*/
int MRISfdr(MRIS *surf, double fdr, int signid, 
	    int log10flag, int maskflag, double *fdrthresh)
{
  double *p=NULL, val, val2null;
  int vtxno, np;

  if(log10flag) val2null = 0;
  else          val2null = 1;

  p = (double *) calloc(surf->nvertices,sizeof(double));
  np = 0;
  for(vtxno = 0; vtxno < surf->nvertices; vtxno++){
    if(maskflag && !surf->vertices[vtxno].undefval) continue;
    val = surf->vertices[vtxno].val;
    if(signid == -1 && val > 0) continue;
    if(signid == +1 && val < 0) continue;
    val = fabs(val);
    if(log10flag) val = pow(10,-val);
    p[np] = val;
    np++;
  }
  printf("np = %d, nv = %d\n",np,surf->nvertices);

  // Check that something met the match criteria, 
  // otherwise return an error
  if(np==0){
    free(p);
    return(1);
  }

  *fdrthresh = fdrthreshold(p,np,fdr);

  // Perform the thresholding
  for(vtxno = 0; vtxno < surf->nvertices; vtxno++){
    if(maskflag && !surf->vertices[vtxno].undefval){
      // Set to null if masking and not in the mask 
      surf->vertices[vtxno].val2 = val2null;
      continue;
    }
    val = surf->vertices[vtxno].val;
    if(signid == -1 && val > 0){
      // Set to null if wrong sign
      surf->vertices[vtxno].val2 = val2null;
      continue;
    }
    if(signid == +1 && val < 0){
      // Set to null if wrong sign
      surf->vertices[vtxno].val2 = val2null;
      continue;
    }

    val = fabs(val);
    if(log10flag) val = pow(10,-val);

    if(val > *fdrthresh){
      // Set to null if greather than thresh
      surf->vertices[vtxno].val2 = val2null;
      continue;
    }

    // Otherwise, it meets all criteria, so 
    // pass the original value through
    surf->vertices[vtxno].val2 = surf->vertices[vtxno].val;
  }

  // Change the fdrthresh to log10 if needed
  if(log10flag) *fdrthresh = -log10(*fdrthresh);
  free(p);

  return(0);
}
