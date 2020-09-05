#include <cstring>
#include <limits>
#include <list>
#include <iostream>

#include "gmapping/scanmatcher/scanmatcher.h"
#include "gmapping/scanmatcher/gridlinetraversal.h"

namespace GMapping
{
using namespace std;

const double ScanMatcher::nullLikelihood=-.5;

ScanMatcher::ScanMatcher(): m_laserPose(0,0,0)
{
    m_laserBeams=0;
    m_optRecursiveIterations=3;
    m_activeAreaComputed=false;
    m_llsamplerange=0.01;
    m_llsamplestep=0.01;
    m_lasamplerange=0.005;
    m_lasamplestep=0.005;
    m_enlargeStep=10.;
    m_fullnessThreshold=0.1;
    m_angularOdometryReliability=0.;
    m_linearOdometryReliability=0.;
    m_freeCellRatio=sqrt(2.);
    m_initialBeamsSkip=0;
    m_linePoints = new IntPoint[20000];
}

ScanMatcher::~ScanMatcher()
{
    delete [] m_linePoints;
}

void ScanMatcher::invalidateActiveArea()
{
    m_activeAreaComputed=false;
}

void ScanMatcher::computeActiveArea(ScanMatcherMap& map, const OrientedPoint& p, const double* readings)
{
    if (m_activeAreaComputed)
        return;

    OrientedPoint lp=p;
    lp.x+=cos(p.theta)*m_laserPose.x-sin(p.theta)*m_laserPose.y;
    lp.y+=sin(p.theta)*m_laserPose.x+cos(p.theta)*m_laserPose.y;
    lp.theta+=m_laserPose.theta;

    IntPoint p0=map.world2map(lp);

    Point min(map.map2world(0,0));
    Point max(map.map2world(map.getMapSizeX()-1,map.getMapSizeY()-1));

    if (lp.x<min.x) min.x=lp.x;
    if (lp.y<min.y) min.y=lp.y;
    if (lp.x>max.x) max.x=lp.x;
    if (lp.y>max.y) max.y=lp.y;
    const double * angle=m_laserAngles+m_initialBeamsSkip;

    for (const double* r=readings+m_initialBeamsSkip; r<readings+m_laserBeams; r++, angle++)
    {
        if (*r>m_laserMaxRange||*r==0.0||isnan(*r)) continue;
        double d=*r>m_usableRange?m_usableRange:*r;
        Point phit=lp;
        phit.x+=d*cos(lp.theta+*angle);
        phit.y+=d*sin(lp.theta+*angle);
        if (phit.x<min.x) min.x=phit.x;
        if (phit.y<min.y) min.y=phit.y;
        if (phit.x>max.x) max.x=phit.x;
        if (phit.y>max.y) max.y=phit.y;
    }
    if ( !map.isInside(min)	|| !map.isInside(max))
    {
        Point lmin(map.map2world(0,0));
        Point lmax(map.map2world(map.getMapSizeX()-1,map.getMapSizeY()-1));
        min.x=( min.x >= lmin.x )? lmin.x: min.x-m_enlargeStep;
        max.x=( max.x <= lmax.x )? lmax.x: max.x+m_enlargeStep;
        min.y=( min.y >= lmin.y )? lmin.y: min.y-m_enlargeStep;
        max.y=( max.y <= lmax.y )? lmax.y: max.y+m_enlargeStep;
        map.resize(min.x, min.y, max.x, max.y);
    }

    HierarchicalArray2D<PointAccumulator>::PointSet activeArea;
    angle=m_laserAngles+m_initialBeamsSkip;
    for (const double* r=readings+m_initialBeamsSkip; r<readings+m_laserBeams; r++, angle++)
    {
        if (m_generateMap)
        {
            double d=*r;
            if (d>m_laserMaxRange||d==0.0||isnan(d))
                    continue;
            if (d>m_usableRange)
                    d=m_usableRange;
            Point phit=lp+Point(d*cos(lp.theta+*angle),d*sin(lp.theta+*angle));
            IntPoint p0=map.world2map(lp);
            IntPoint p1=map.world2map(phit);
            GridLineTraversalLine line;
            line.points=m_linePoints;
            GridLineTraversal::gridLine(p0, p1, &line);
            for (int i=0; i<line.num_points-1; i++)
            {
                assert(map.isInside(m_linePoints[i]));
                activeArea.insert(map.storage().patchIndexes(m_linePoints[i]));
                assert(m_linePoints[i].x>=0 && m_linePoints[i].y>=0);
            }
            if (d<m_usableRange)
            {
                IntPoint cp=map.storage().patchIndexes(p1);
                assert(cp.x>=0 && cp.y>=0);
                activeArea.insert(cp);
            }
        }
        else
        {
            if (*r>m_laserMaxRange||*r>m_usableRange||*r==0.0||isnan(*r)) continue;
            Point phit=lp;
            phit.x+=*r*cos(lp.theta+*angle);
            phit.y+=*r*sin(lp.theta+*angle);
            IntPoint p1=map.world2map(phit);
            assert(p1.x>=0 && p1.y>=0);
            IntPoint cp=map.storage().patchIndexes(p1);
            assert(cp.x>=0 && cp.y>=0);
            activeArea.insert(cp);
        }
    }
    map.storage().setActiveArea(activeArea, true);
    m_activeAreaComputed=true;
}

double ScanMatcher::registerScan(ScanMatcherMap& map, const OrientedPoint& p, const double* readings)
{
    if (!m_activeAreaComputed)
        computeActiveArea(map, p, readings);
    map.storage().allocActiveArea();

    // lp：laser center in world coordinate
    OrientedPoint lp=p;
    lp.x+=cos(p.theta)*m_laserPose.x-sin(p.theta)*m_laserPose.y;
    lp.y+=sin(p.theta)*m_laserPose.x+cos(p.theta)*m_laserPose.y;
    lp.theta+=m_laserPose.theta;

    // p0：laser center in map coordinate
    IntPoint p0=map.world2map(lp);

    const double * angle=m_laserAngles+m_initialBeamsSkip;

    // for each scan line
    for (const double* r=readings+m_initialBeamsSkip; r<readings+m_laserBeams; r++, angle++)
    {
        if (m_generateMap) // update an existing map
        {
            double d=*r;
            if (d>m_laserMaxRange||d==0.0||isnan(d)) // not hit a object, or data error
                continue;
            if (d>m_usableRange)
                d=m_usableRange;

            // phit: the hit point
            Point phit=lp+Point(d*cos(lp.theta+*angle),d*sin(lp.theta+*angle));

            // p1: phit in map coordinate
            IntPoint p1=map.world2map(phit);

            GridLineTraversalLine line;
            line.points=m_linePoints;

            // line: the all grids between laser center and hit point in map coordinate
            GridLineTraversal::gridLine(p0, p1, &line);
            for (int i=0; i<line.num_points-1; i++)
            {
                // TSDF mapping algorithm only update the closest grid of hit point
                if(abs(line.points[i].x-p1.x)<=1 && abs(line.points[i].y-p1.y)<=1)
                {
                    map.cell(line.points[i]).update(false, Point(0,0));
                }

                // occupancy grid map update all grid on this line
                PointAccumulator& cell=map.cell(line.points[i]);
                cell.update(false, Point(0,0));
            }
            if (d<m_usableRange)
            {
                // both TSDF mapping and occupancy grid map should update the hit point
                map.cell(p1).update(true, phit);
            }
        }
        else // generate a new map
        {
            if (*r>m_laserMaxRange||*r>m_usableRange||*r==0.0||isnan(*r)) continue;
            Point phit=lp;
            phit.x+=*r*cos(lp.theta+*angle);
            phit.y+=*r*sin(lp.theta+*angle);
            IntPoint p1=map.world2map(phit);
            assert(p1.x>=0 && p1.y>=0);
            // update the hit point
            map.cell(p1).update(true,phit);
        }
    }
    return 0;
}

double ScanMatcher::icpOptimize(OrientedPoint& pnew, const ScanMatcherMap& map, const OrientedPoint& init, const double* readings) const
{
    double currentScore;
    double sc=score(map, init, readings);;
    OrientedPoint start=init;
    pnew=init;
    int iterations=0;
    do{
            currentScore=sc;
            sc=icpStep(pnew, map, start, readings);
            start=pnew;
            iterations++;
    } while (sc>currentScore);
    return currentScore;
}

double ScanMatcher::optimize(OrientedPoint& pnew, const ScanMatcherMap& map, const OrientedPoint& init, const double* readings) const
{
    double bestScore=-1;
    OrientedPoint currentPose=init;
    double currentScore=score(map, currentPose, readings);
    double adelta=m_optAngularDelta, ldelta=m_optLinearDelta;
    unsigned int refinement=0;
    enum Move{Front, Back, Left, Right, TurnLeft, TurnRight, Done};
    int c_iterations=0;
    do{
        if (bestScore>=currentScore)
        {
            refinement++;
            adelta*=.5;
            ldelta*=.5;
        }
        bestScore=currentScore;
        OrientedPoint bestLocalPose=currentPose;
        OrientedPoint localPose=currentPose;
        Move move=Front;
        do {
            localPose=currentPose;
            switch(move)
            {
            case Front:
                localPose.x+=ldelta;
                move=Back;
                break;
            case Back:
                localPose.x-=ldelta;
                move=Left;
                break;
            case Left:
                localPose.y-=ldelta;
                move=Right;
                break;
            case Right:
                localPose.y+=ldelta;
                move=TurnLeft;
                break;
            case TurnLeft:
                localPose.theta+=adelta;
                move=TurnRight;
                break;
            case TurnRight:
                localPose.theta-=adelta;
                move=Done;
                break;
            default:;
            }
            double odo_gain=1;
            if (m_angularOdometryReliability>0.)
            {
                double dth=init.theta-localPose.theta; 	dth=atan2(sin(dth), cos(dth)); 	dth*=dth;
                odo_gain*=exp(-m_angularOdometryReliability*dth);
            }
            if (m_linearOdometryReliability>0.)
            {
                double dx=init.x-localPose.x;
                double dy=init.y-localPose.y;
                double drho=dx*dx+dy*dy;
                odo_gain*=exp(-m_linearOdometryReliability*drho);
            }
            double localScore=odo_gain*score(map, localPose, readings);
            if (localScore>currentScore)
            {
                currentScore=localScore;
                bestLocalPose=localPose;
            }
            c_iterations++;
        } while(move!=Done);
        currentPose=bestLocalPose;
    }while (currentScore>bestScore || refinement<m_optRecursiveIterations);
    pnew=currentPose;
    return bestScore;
}

struct ScoredMove
{
	OrientedPoint pose;
	double score;
	double likelihood;
};

typedef std::list<ScoredMove> ScoredMoveList;

double ScanMatcher::optimize(OrientedPoint& _mean, ScanMatcher::CovarianceMatrix& _cov, const ScanMatcherMap& map, const OrientedPoint& init, const double* readings) const
{
	ScoredMoveList moveList;
	double bestScore=-1;
	OrientedPoint currentPose=init;
        ScoredMove sm={currentPose,0,0};
	unsigned int matched=likelihoodAndScore(sm.score, sm.likelihood, map, currentPose, readings);
	double currentScore=sm.score;
	moveList.push_back(sm);
	double adelta=m_optAngularDelta, ldelta=m_optLinearDelta;
	unsigned int refinement=0;
	int count=0;
	enum Move{Front, Back, Left, Right, TurnLeft, TurnRight, Done};
	do{
        if (bestScore>=currentScore)
        {
            refinement++;
            adelta*=.5;
            ldelta*=.5;
        }
        bestScore=currentScore;
        OrientedPoint bestLocalPose=currentPose;
        OrientedPoint localPose=currentPose;

        Move move=Front;
        do
        {
            localPose=currentPose;
            switch(move)
            {
            case Front:
                localPose.x+=ldelta;
                move=Back;
                break;
            case Back:
                localPose.x-=ldelta;
                move=Left;
                break;
            case Left:
                localPose.y-=ldelta;
                move=Right;
                break;
            case Right:
                localPose.y+=ldelta;
                move=TurnLeft;
                break;
            case TurnLeft:
                localPose.theta+=adelta;
                move=TurnRight;
                break;
            case TurnRight:
                localPose.theta-=adelta;
                move=Done;
                break;
            default:;
            }
            double localScore, localLikelihood;
            double odo_gain=1;
            if (m_angularOdometryReliability>0.)
            {
                double dth=init.theta-localPose.theta;
                dth=atan2(sin(dth), cos(dth));
                dth*=dth;
                odo_gain*=exp(-m_angularOdometryReliability*dth);
            }
            if (m_linearOdometryReliability>0.)
            {
                double dx=init.x-localPose.x;
                double dy=init.y-localPose.y;
                double drho=dx*dx+dy*dy;
                odo_gain*=exp(-m_linearOdometryReliability*drho);
            }
            localScore=odo_gain*score(map, localPose, readings);
            count++;
            matched=likelihoodAndScore(localScore, localLikelihood, map, localPose, readings);
            if (localScore>currentScore)
            {
                currentScore=localScore;
                bestLocalPose=localPose;
            }
            sm.score=localScore;
            sm.likelihood=localLikelihood;//+log(odo_gain);
            sm.pose=localPose;
            moveList.push_back(sm);
        } while(move!=Done);
        currentPose=bestLocalPose;
        }while (currentScore>bestScore || refinement<m_optRecursiveIterations);
	double lmin=1e9;
	double lmax=-1e9;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        lmin=it->likelihood<lmin?it->likelihood:lmin;
        lmax=it->likelihood>lmax?it->likelihood:lmax;
    }
    for (ScoredMoveList::iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        it->likelihood=exp(it->likelihood-lmax);
    }
    OrientedPoint mean(0,0,0);
    double lacc=0;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        mean=mean+it->pose*it->likelihood;
        lacc+=it->likelihood;
    }
    mean=mean*(1./lacc);
    CovarianceMatrix cov={0.,0.,0.,0.,0.,0.};
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        OrientedPoint delta=it->pose-mean;
        delta.theta=atan2(sin(delta.theta), cos(delta.theta));
        cov.xx+=delta.x*delta.x*it->likelihood;
        cov.yy+=delta.y*delta.y*it->likelihood;
        cov.tt+=delta.theta*delta.theta*it->likelihood;
        cov.xy+=delta.x*delta.y*it->likelihood;
        cov.xt+=delta.x*delta.theta*it->likelihood;
        cov.yt+=delta.y*delta.theta*it->likelihood;
    }
    cov.xx/=lacc, cov.xy/=lacc, cov.xt/=lacc, cov.yy/=lacc, cov.yt/=lacc, cov.tt/=lacc;
    _mean=currentPose;
    _cov=cov;
    return bestScore;
}

void ScanMatcher::setLaserParameters(unsigned int beams, double* angles, const OrientedPoint& lpose)
{
    assert(beams<LASER_MAXBEAMS);
    m_laserPose=lpose;
    m_laserBeams=beams;
    memcpy(m_laserAngles, angles, sizeof(double)*m_laserBeams);
}
	

double ScanMatcher::likelihood(double& _lmax, OrientedPoint& _mean, CovarianceMatrix& _cov, const ScanMatcherMap& map, const OrientedPoint& p, const double* readings)
{
    ScoredMoveList moveList;
    for (double xx=-m_llsamplerange; xx<=m_llsamplerange; xx+=m_llsamplestep)
    for (double yy=-m_llsamplerange; yy<=m_llsamplerange; yy+=m_llsamplestep)
    for (double tt=-m_lasamplerange; tt<=m_lasamplerange; tt+=m_lasamplestep)
    {
        OrientedPoint rp=p;
        rp.x+=xx;
        rp.y+=yy;
        rp.theta+=tt;

        ScoredMove sm;
        sm.pose=rp;

        likelihoodAndScore(sm.score, sm.likelihood, map, rp, readings);
        moveList.push_back(sm);
    }
    double lmax=-1e9;
    double lcum=0;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        lmax=it->likelihood>lmax?it->likelihood:lmax;
    }
    for (ScoredMoveList::iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        lcum+=exp(it->likelihood-lmax);
        it->likelihood=exp(it->likelihood-lmax);
    }
    OrientedPoint mean(0,0,0);
    double s=0,c=0;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        mean=mean+it->pose*it->likelihood;
        s+=it->likelihood*sin(it->pose.theta);
        c+=it->likelihood*cos(it->pose.theta);
    }
    mean=mean*(1./lcum);
    s/=lcum;
    c/=lcum;
    mean.theta=atan2(s,c);

    CovarianceMatrix cov={0.,0.,0.,0.,0.,0.};
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        OrientedPoint delta=it->pose-mean;
        delta.theta=atan2(sin(delta.theta), cos(delta.theta));
        cov.xx+=delta.x*delta.x*it->likelihood;
        cov.yy+=delta.y*delta.y*it->likelihood;
        cov.tt+=delta.theta*delta.theta*it->likelihood;
        cov.xy+=delta.x*delta.y*it->likelihood;
        cov.xt+=delta.x*delta.theta*it->likelihood;
        cov.yt+=delta.y*delta.theta*it->likelihood;
    }
    cov.xx/=lcum, cov.xy/=lcum, cov.xt/=lcum, cov.yy/=lcum, cov.yt/=lcum, cov.tt/=lcum;
    _mean=mean;
    _cov=cov;
    _lmax=lmax;
    return log(lcum)+lmax;
}

double ScanMatcher::likelihood(double& _lmax, OrientedPoint& _mean, CovarianceMatrix& _cov, const ScanMatcherMap& map,
                               const OrientedPoint& p, Gaussian3& odometry, const double* readings, double gain)
{
    ScoredMoveList moveList;
	
    for (double xx=-m_llsamplerange; xx<=m_llsamplerange; xx+=m_llsamplestep)
    for (double yy=-m_llsamplerange; yy<=m_llsamplerange; yy+=m_llsamplestep)
    for (double tt=-m_lasamplerange; tt<=m_lasamplerange; tt+=m_lasamplestep)
    {
        OrientedPoint rp=p;
        rp.x+=xx;
        rp.y+=yy;
        rp.theta+=tt;

        ScoredMove sm;
        sm.pose=rp;

        likelihoodAndScore(sm.score, sm.likelihood, map, rp, readings);
        sm.likelihood+=odometry.eval(rp)/gain;
        assert(!isnan(sm.likelihood));
        moveList.push_back(sm);
    }
    double lmax=-std::numeric_limits<double>::max();
    double lcum=0;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        lmax=it->likelihood>lmax?it->likelihood:lmax;
    }
    for (ScoredMoveList::iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        lcum+=exp(it->likelihood-lmax);
        it->likelihood=exp(it->likelihood-lmax);
    }
    OrientedPoint mean(0,0,0);
    double s=0,c=0;
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        mean=mean+it->pose*it->likelihood;
        s+=it->likelihood*sin(it->pose.theta);
        c+=it->likelihood*cos(it->pose.theta);
    }
    mean=mean*(1./lcum);
    s/=lcum;
    c/=lcum;
    mean.theta=atan2(s,c);
	
    CovarianceMatrix cov={0.,0.,0.,0.,0.,0.};
    for (ScoredMoveList::const_iterator it=moveList.begin(); it!=moveList.end(); it++)
    {
        OrientedPoint delta=it->pose-mean;
        delta.theta=atan2(sin(delta.theta), cos(delta.theta));
        cov.xx+=delta.x*delta.x*it->likelihood;
        cov.yy+=delta.y*delta.y*it->likelihood;
        cov.tt+=delta.theta*delta.theta*it->likelihood;
        cov.xy+=delta.x*delta.y*it->likelihood;
        cov.xt+=delta.x*delta.theta*it->likelihood;
        cov.yt+=delta.y*delta.theta*it->likelihood;
    }
    cov.xx/=lcum, cov.xy/=lcum, cov.xt/=lcum, cov.yy/=lcum, cov.yt/=lcum, cov.tt/=lcum;
    _mean=mean;
    _cov=cov;
    _lmax=lmax;
    double v=log(lcum)+lmax;
    assert(!isnan(v));
    return v;
}

void ScanMatcher::setMatchingParameters(double urange, double range, double sigma, int kernsize, double lopt, double aopt, int iterations,  double likelihoodSigma, unsigned int likelihoodSkip)
{
    m_usableRange=urange;
    m_laserMaxRange=range;
    m_kernelSize=kernsize;
    m_optLinearDelta=lopt;
    m_optAngularDelta=aopt;
    m_optRecursiveIterations=iterations;
    m_gaussianSigma=sigma;
    m_likelihoodSigma=likelihoodSigma;
    m_likelihoodSkip=likelihoodSkip;
}

};

