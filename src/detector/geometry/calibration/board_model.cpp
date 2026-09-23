#include "board_model.hpp"
#include "geometry_calibration.hpp"
#include "wire_model.hpp"
#include "wire_processing.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <fstream>
#include <nlohmann/json.hpp>

namespace board_model
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr double sectorAngle = pi / 10;
    double angle(cv::Point2d p) { return std::atan2(p.x, p.y); }
    double wrap(double a) { return std::remainder(a, 2 * pi); }
    cv::Point2d radial(double a, double r) { return {r * std::sin(a), r * std::cos(a)}; }
    bool finite(cv::Point2d p) { return std::isfinite(p.x) && std::isfinite(p.y); }
    double residual(const cv::Matx33d &inverse, const Observation &o)
    {
        const auto p = map(inverse, o.image);
        if (!finite(p)) return 1e6;
        if (o.kind == Kind::Ring || o.kind == Kind::BandCentre) return cv::norm(p) - o.target;
        if (o.kind == Kind::Wire) return p.x * std::cos(o.target) - p.y * std::sin(o.target);
        // Centre observations are split into X and Y components, target 0 or 1.
        return o.target == 0 ? p.x : p.y;
    }
    double quantile(std::vector<double> x, double q)
    {
        if (x.empty()) return std::numeric_limits<double>::infinity();
        std::sort(x.begin(), x.end());
        return x[(size_t)std::floor(q * (x.size() - 1))];
    }
    cv::Matx33d unpack(const cv::Mat &v)
    {
        return {v.at<double>(0),v.at<double>(1),v.at<double>(2),
                v.at<double>(3),v.at<double>(4),v.at<double>(5),
                v.at<double>(6),v.at<double>(7),1.0};
    }
    cv::Mat pack(cv::Matx33d m)
    {
        m *= 1.0 / m(2,2);
        cv::Mat p(8,1,CV_64F);
        for (int i=0;i<8;++i) p.at<double>(i)=m.val[i];
        return p;
    }
    double cost(const cv::Matx33d &inverse, const std::vector<Observation> &obs, double delta)
    {
        double sum=0;
        for (const auto &o:obs) if (!o.heldOut)
        {
            double a=std::abs(residual(inverse,o));
            sum += a<=delta ? 0.5*a*a : delta*(a-0.5*delta);
        }
        return sum;
    }
    double evidenceAt(const cv::Mat &mask, cv::Point2d p)
    {
        if (!finite(p) || p.x<1 || p.y<1 || p.x>=mask.cols-2 || p.y>=mask.rows-2) return 0;
        const int x=(int)p.x,y=(int)p.y;
        const double dx=p.x-x,dy=p.y-y;
        return ((1-dx)*(1-dy)*mask.at<float>(y,x)+dx*(1-dy)*mask.at<float>(y,x+1)+
                (1-dx)*dy*mask.at<float>(y+1,x)+dx*dy*mask.at<float>(y+1,x+1));
    }
    std::string plainText(const std::string &value)
    {
        std::string out;
        for(size_t i=0;i<value.size();++i)
        {
            unsigned char c=value[i];
            if(c==27 && i+1<value.size() && value[i+1]=='[')
            { i+=2;while(i<value.size() && !(value[i]>='@' && value[i]<='~'))++i;continue; }
            if(c>=32 && c!=127)out+=value[i];
        }
        return out;
    }
    Profile readProfile(const nlohmann::json &n)
    {
        Profile p;
        p.id=n.at("id").get<std::string>();p.version=n.at("version").get<int>();
        if(n.at("radii_mm").size()!=6 || n.at("numbers_clockwise").size()!=20)
            throw std::runtime_error("board profile needs six radii and twenty numbers");
        p.radii=n.at("radii_mm").get<std::array<double,6>>();
        p.numbers=n.at("numbers_clockwise").get<std::array<int,20>>();
        p.rimRadius=n.at("rim_radius_mm").get<double>();
        p.boundaryToleranceMm=n.at("boundary_tolerance_mm").get<double>();
        if(!p.valid())throw std::runtime_error("invalid physical board profile");
        return p;
    }
    bool sameProfile(const Profile &a,const Profile &b)
    {
        return a.id==b.id && a.version==b.version && a.radii==b.radii && a.numbers==b.numbers &&
               a.rimRadius==b.rimRadius && a.boundaryToleranceMm==b.boundaryToleranceMm;
    }
}
bool Profile::valid() const
{
    double prev=0;
    for(double r:radii) { if(!std::isfinite(r)||r<=prev) return false; prev=r; }
    const std::set<int> seq(numbers.begin(),numbers.end());
    return !id.empty() && version==1 && numbers[0]==20 && numbers[5]==6 && seq.size()==20 && *seq.begin()==1 && *seq.rbegin()==20 &&
           std::isfinite(rimRadius) && rimRadius>radii.back() &&
           std::isfinite(boundaryToleranceMm) && boundaryToleranceMm>0 &&
           boundaryToleranceMm < std::min(radii[3]-radii[2],radii[5]-radii[4])*0.5;
}
Profile Profile::load(const std::string &path)
{
    std::ifstream file(path);
    if(!file)throw std::runtime_error("cannot open board profile: "+path);
    nlohmann::json document;file>>document;
    return readProfile(document.at("profile"));
}
bool enabled()
{
    const char *p=std::getenv("OD_BOARD_MODEL");
    return p && std::string(p)=="physical";
}

std::vector<Observation> observe(const cv::Mat &colourMask,const Model &seed,
                                const std::vector<cv::Point2f> &wires,cv::Point2d bull,bool hasBull)
{
    std::vector<Observation> obs;
    if(colourMask.empty()||!seed.profile.valid()) return obs;
    cv::Mat mask,clipped;
    if(colourMask.channels()==3)
    {
        mask=cv::Mat(colourMask.size(),CV_32F);
        clipped=cv::Mat(colourMask.size(),CV_32F);
        for(int y=0;y<mask.rows;++y)for(int x=0;x<mask.cols;++x)
        {
            const auto p=colourMask.at<cv::Vec3b>(y,x);
            // Red/green opponent contrast cancels warm white illumination. Do not
            // divide by the pixel brightness: that amplifies chroma in dark sisal.
            // The local profile supplies exposure-dependent low/high levels below.
            mask.at<float>(y,x)=std::abs((float)p[2]-(float)p[1])/255.f;
            clipped.at<float>(y,x)=std::max(p[1],p[2])>=250?1.f:0.f;
        }
    }
    else colourMask.convertTo(mask,CV_32F,1./255);
    // Search observed colour runs along many rays. The seed chooses a SEARCH corridor;
    // a boundary is the 50% colour crossing, not a point predicted by the model.
    // Paired edges give ring identity. A single bright pixel cannot supply a ring pair.
    constexpr int rays=240;
    constexpr double stepMm=0.25;
    for(int j=0;j<rays;++j)
    {
        const double a=(j+0.5)*2*pi/rays;
        const int sector=(int)std::floor((a+sectorAngle*0.5)/sectorAngle)%20;
        // Reserve entire alternating 3-degree angular blocks, rather than interleaved
        // pixels on one edge, for validation. None enter the optimizer below.
        const bool hold=(j/2)%3==0;
        for(int pair=0;pair<2;++pair)
        {
            const int inner=pair==0?2:4,outer=inner+1;
            const double ri=seed.profile.radii[inner],ro=seed.profile.radii[outer],width=ro-ri;
            const double low=ri-2*width,high=ro+2*width;
            std::vector<double> samples;
            for(double r=low;r<=high;r+=stepMm)
                samples.push_back(evidenceAt(mask,map(seed.boardToImage,radial(a,r))));
            const double dark=quantile(samples,0.25),bright=quantile(samples,0.95);
            if(bright-dark<0.10) continue; // no visible chromatic edge to measure
            const double threshold=(dark+bright)*0.5;
            double start=-1,previous=samples.front(),best=-1,bestError=1e30,bestEnd=0;
            for(size_t k=1;k<samples.size();++k)
            {
                const double r=low+k*stepMm,v=samples[k];
                if(v>=threshold && previous<threshold)
                    start=r-stepMm+stepMm*(threshold-previous)/(v-previous);
                if(v<threshold && previous>=threshold && start>=low)
                {
                    const double end=r-stepMm+stepMm*(previous-threshold)/(previous-v);
                    const double span=end-start;
                    const double error=std::abs((start+end-ri-ro)*0.5);
                    if(span>=width*0.5 && span<=width*1.5 && error<=width && error<bestError)
                    { best=start;bestEnd=end;bestError=error; }
                    start=-1;
                }
                previous=v;
            }
            if(best<0) continue;
            if(!clipped.empty())
            {
                double fraction=0;
                for(int k=0;k<9;++k)
                {
                    double r=best+(bestEnd-best)*(0.25+0.5*k/8);
                    fraction+=evidenceAt(clipped,map(seed.boardToImage,radial(a,r)));
                }
                // Once the dominant colour clips, its chroma crossing is not an
                // observed wire location. Use distributed unsaturated arcs instead.
                if(fraction/9>0.5)continue;
            }
            // Chroma blur changes apparent band width, with opposite shifts of its
            // two edges. Their centre is the ring landmark; the physical profile
            // supplies the scoring boundaries. Do not call a colour fringe a wire.
            obs.push_back({map(seed.boardToImage,radial(a,(best+bestEnd)*0.5)),
                           Kind::BandCentre,(ri+ro)*0.5,inner,sector,hold,
                           map(seed.boardToImage,radial(a,best)),map(seed.boardToImage,radial(a,bestEnd))});
        }
    }
    if(colourMask.channels()==3)
    {
        // Radial boundaries are observed in the black/white single beds, away
        // from ring colours and their compressed chroma fringes.
        cv::Mat light;cv::cvtColor(colourMask,light,cv::COLOR_BGR2GRAY);
        light.convertTo(light,CV_32F,1./255);
        for(int sector=0;sector<20;++sector)
        {
            const double target=(sector-0.5)*sectorAngle;
            for(double radius:{55.,75.,90.,120.,140.,150.})
            {
                constexpr int steps=60;
                const double half=sectorAngle*0.25,increment=2*half/steps;
                std::vector<double> values;
                for(int k=0;k<=steps;++k)
                {
                    double a=target-half+k*increment,v=0;
                    for(double dr:{-1.,0.,1.})v+=evidenceAt(light,map(seed.boardToImage,radial(a,radius+dr)));
                    values.push_back(v/3);
                }
                const double low=quantile(values,0.2),high=quantile(values,0.8);
                if(high-low<0.2)continue;
                const double threshold=(low+high)/2;
                double bestAngle=0,strongest=0;
                for(int k=1;k<=steps;++k)
                {
                    double left=values[k-1],right=values[k];
                    const bool crossing=sector%2?(left<threshold&&right>=threshold):(left>=threshold&&right<threshold);
                    if(crossing&&std::abs(right-left)>strongest)
                    { strongest=std::abs(right-left);bestAngle=target-half+(k-1+(threshold-left)/(right-left))*increment; }
                }
                if(strongest>0)
                    obs.push_back({map(seed.boardToImage,radial(bestAngle,radius)),Kind::Wire,target,-1,sector,sector%3==0});
            }
        }
    }
    else
    {
    // Use actual pre-model wire candidates, never ringFrom()'s generated endpoints.
    for(size_t i=0;i<wires.size();++i)
    {
        cv::Point2d p=map(seed.imageToBoard,wires[i]);
        if(!finite(p)||cv::norm(p)<seed.profile.radii[2]*0.5) continue;
        double a=angle(p),target=std::round((a+sectorAngle*0.5)/sectorAngle)*sectorAngle-sectorAngle*0.5;
        if(std::abs(wrap(a-target))>3*pi/180) continue;
        int sector=((int)std::lround((target+sectorAngle*0.5)/sectorAngle)%20+20)%20;
        obs.push_back({wires[i],Kind::Wire,target,-1,sector,sector%3==0});
    }
    }
    if(hasBull && finite(bull))
    {
        obs.push_back({bull,Kind::Centre,0,-1,-1,false});
        obs.push_back({bull,Kind::Centre,1,-1,-1,false});
    }
    return obs;
}
Model fit(const Model &seed,const std::vector<Observation> &obs)
{
    Model out=seed;out.valid=false;out.quality=Quality();
    if(!seed.profile.valid() || !seed.anchored || seed.imageSize.width<=0 || seed.imageSize.height<=0)
    {out.reason="no valid profile, image size or independently supplied numbered anchor";return out;}
    bool invertible=false;seed.boardToImage.inv(cv::DECOMP_LU,&invertible);
    if(!invertible) {out.reason="singular initial board mapping";return out;}
    const double extent=seed.profile.radii.back();
    const double imageScale=std::max(seed.imageSize.width,seed.imageSize.height);
    const cv::Matx33d N(1/imageScale,0,-seed.imageSize.width/(2*imageScale),
                       0,1/imageScale,-seed.imageSize.height/(2*imageScale),0,0,1);
    const cv::Matx33d S(extent,0,0,0,extent,0,0,0,1);
    cv::Mat params=pack(S.inv()*seed.imageToBoard*N.inv());
    auto inverseOf=[&](const cv::Mat &p){return S*unpack(p)*N;};
    for(const auto &o:obs)
    {
        if(!finite(o.image)||!std::isfinite(o.target)) {out.reason="non-finite observation";return out;}
        if(o.heldOut) ++out.quality.heldOut;else ++out.quality.training;
    }
    if(out.quality.training<40 || out.quality.heldOut<20)
    {out.reason="insufficient independent training/held-out support";return out;}
    // Numerical Gauss-Newton on eight normalized projective parameters, Huber weights.
    // Optimization never consumes held-out observations. Damping and a line search
    // prevent a bad observation from moving the map through a projective horizon.
    const double delta=seed.profile.boundaryToleranceMm*0.5;
    cv::Mat lastNormal;
    double lambda=1e-5;
    for(int iter=0;iter<40;++iter)
    {
        cv::Mat A=cv::Mat::zeros(8,8,CV_64F),b=cv::Mat::zeros(8,1,CV_64F);
        const auto inv=inverseOf(params);
        for(const auto &o:obs) if(!o.heldOut)
        {
            const double e=residual(inv,o),w=std::abs(e)>delta?delta/std::abs(e):1;
            double jac[8];
            for(int k=0;k<8;++k)
            {
                cv::Mat pert=params.clone();pert.at<double>(k)+=1e-6;
                jac[k]=(residual(inverseOf(pert),o)-e)/1e-6;
            }
            for(int k=0;k<8;++k)
            {b.at<double>(k)-=w*jac[k]*e;for(int l=0;l<8;++l) A.at<double>(k,l)+=w*jac[k]*jac[l];}
        }
        lastNormal=A.clone();
        for(int k=0;k<8;++k) A.at<double>(k,k)+=lambda*(1+A.at<double>(k,k));
        cv::Mat change;
        if(!cv::solve(A,b,change,cv::DECOMP_SVD)) break;
        const double oldCost=cost(inv,obs,delta);
        bool accepted=false;
        for(double scale=1;scale>=1.0/128;scale*=0.5)
        {
            cv::Mat candidate=params+change*scale;
            if(cost(inverseOf(candidate),obs,delta)<oldCost)
            {params=candidate;accepted=true;lambda=std::max(1e-9,lambda*0.3);break;}
        }
        if(!accepted){lambda*=10;if(lambda>1e7) break;}
        if(cv::norm(change)<1e-9) break;
    }
    out.imageToBoard=inverseOf(params);
    out.boardToImage=out.imageToBoard.inv(cv::DECOMP_LU,&invertible);
    if(!invertible || !cv::checkRange(cv::Mat(out.boardToImage)))
    {out.reason="fitted board mapping is singular or non-finite";return out;}
    // A front view must preserve the named board's clockwise number order in image
    // coordinates (world Y points up). Reject mirrors and a horizon across the board.
    const auto centre=map(out.boardToImage,{0,0});
    auto dx=map(out.boardToImage,{1,0})-centre,dy=map(out.boardToImage,{0,1})-centre;
    if(dx.x*dy.y-dx.y*dy.x>=0) {out.reason="wrong handedness";return out;}
    double horizonSign=0;
    for(int j=0;j<40;++j)
    {
        auto p=radial(j*2*pi/40,extent);
        const auto q=out.boardToImage*cv::Vec3d(p.x,p.y,1);
        if(j==0)horizonSign=q[2];
        if(!std::isfinite(q[2])||q[2]*horizonSign<=0)
        {out.reason="projective horizon crosses scoring area";return out;}
    }
    const auto anchorImage=map(seed.boardToImage,{0,extent});
    out.quality.orientationErrorDeg=std::abs(wrap(angle(map(out.imageToBoard,anchorImage))))*180/pi;
    if(out.quality.orientationErrorDeg>4.5)
    {out.reason="fitted rotation contradicts the numbered anchor";return out;}
    std::set<int> sectors,quadrants;
    double trainSq=0,holdSq=0,pixelSq=0;std::vector<double> heldErrors,colourEdgeErrors;
    for(const auto &o:obs)
    {
        const double e=residual(out.imageToBoard,o);
        if(o.kind==Kind::Wire)++out.quality.radialWires;
        if(o.kind==Kind::BandCentre)
        {
            ++out.quality.bandCentres;
            if(o.heldOut && o.ring>=0 && o.ring<5)
            {
                colourEdgeErrors.push_back(std::abs(cv::norm(map(out.imageToBoard,o.bandInnerImage))-seed.profile.radii[o.ring]));
                colourEdgeErrors.push_back(std::abs(cv::norm(map(out.imageToBoard,o.bandOuterImage))-seed.profile.radii[o.ring+1]));
            }
        }
        if(!o.heldOut)trainSq+=e*e;
        else
        {
            holdSq+=e*e;heldErrors.push_back(std::abs(e));
            auto px=o,py=o;px.image.x+=0.5;py.image.y+=0.5;
            double gx=2*(residual(out.imageToBoard,px)-e),gy=2*(residual(out.imageToBoard,py)-e);
            const double gradient=std::hypot(gx,gy);
            pixelSq+=gradient>1e-9?e*e/(gradient*gradient):1e12;
        }
        if((o.kind==Kind::Ring || o.kind==Kind::BandCentre) && std::abs(e)<=seed.profile.boundaryToleranceMm)
        {
            if(o.kind==Kind::Ring && o.ring>=0&&o.ring<6)++out.quality.ringSupport[o.ring];
            if(o.kind==Kind::BandCentre && (o.ring==2||o.ring==4))++out.quality.bandSupport[o.ring/2-1];
            if(o.sector>=0){sectors.insert(o.sector);quadrants.insert(o.sector/5);}
        }
    }
    out.quality.trainRmsMm=std::sqrt(trainSq/out.quality.training);
    out.quality.heldOutRmsMm=std::sqrt(holdSq/out.quality.heldOut);
    out.quality.heldOutRmsPx=std::sqrt(pixelSq/out.quality.heldOut);
    out.quality.heldOutP95Mm=quantile(heldErrors,0.95);
    out.quality.colourEdgeP95Mm=quantile(colourEdgeErrors,0.95);
    out.quality.sectors=(int)sectors.size();out.quality.quadrants=(int)quadrants.size();
    // Linearized fit uncertainty, explicitly separate from held-out model/lens error.
    out.quality.parameterSigmaMm=std::numeric_limits<double>::infinity();
    if(!lastNormal.empty())
    {
        cv::Mat covariance;double condition=cv::invert(lastNormal,covariance,cv::DECOMP_SVD);
        if(condition>1e-12)
        {
            double worst=0;
            for(int j=0;j<20;++j)
            {
                const auto p=map(out.boardToImage,radial(j*sectorAngle,extent));
                const auto base=map(out.imageToBoard,p);
                cv::Mat J(2,8,CV_64F);
                for(int k=0;k<8;++k)
                {
                    cv::Mat v=params.clone();v.at<double>(k)+=1e-6;
                    auto q=map(inverseOf(v),p);
                    J.at<double>(0,k)=(q.x-base.x)/1e-6;J.at<double>(1,k)=(q.y-base.y)/1e-6;
                }
                cv::Mat cov=J*covariance*J.t();
                worst=std::max(worst,cv::trace(cov)[0]*trainSq/std::max(1,out.quality.training-8));
            }
            out.quality.parameterSigmaMm=std::sqrt(worst);
        }
    }
    int largestGap=0,run=0;
    for(int j=0;j<40;++j)
    { if(sectors.count(j%20))run=0;else largestGap=std::max(largestGap,++run); }
    if(out.quality.sectors<8 || largestGap>5 || out.quality.quadrants<3 ||
       (out.quality.bandSupport[0]<24 && (out.quality.ringSupport[2]<24 || out.quality.ringSupport[3]<24)) ||
       (out.quality.bandSupport[1]<24 && (out.quality.ringSupport[4]<24 || out.quality.ringSupport[5]<24)))
        out.reason="insufficient distributed treble/double support; ring identity not established";
    else if(out.quality.heldOutP95Mm>seed.profile.boundaryToleranceMm ||
            out.quality.parameterSigmaMm>seed.profile.boundaryToleranceMm)
        out.reason="held-out residual or fit uncertainty exceeds profile budget; lens/model/identity unvalidated";
    else {out.valid=true;out.reason="accepted on observed landmarks; scoring circles model-predicted";}
    return out;
}
Measurement measure(const cv::Mat &frame,const DartboardCalibration &calib,const Profile &profile)
{
    Measurement out;Model &seed=out.model;seed.profile=profile;seed.imageSize=frame.size();
    seed.cameraIdentity="camera-slot-"+std::to_string(calib.camera_index);
    seed.anchored=orientation_processing::wedgeCanBeRead(calib.orientation);
    seed.anchorSource=plainText(orientation_processing::howItReads(calib.orientation));
    if(frame.empty()||!calib.sees_board||!seed.anchored||calib.wires.wireEndpoints.size()!=20)
    {seed.reason="initial board or independently measured numbered orientation unavailable";return out;}
    const auto plane=wire_model::planeOf(calib.ellipses.outerDoubleEllipse,calib.bullCenter,
                                         wire_processing::conicOfDoublesFor(calib));
    if(!plane.built){seed.reason="initial board plane unavailable";return out;}
    const int start=calib.orientation.wedge20WireIndex;
    if(start<0||start>=20){seed.reason="numbered anchor outside wire ring";return out;}
    const double theta=wire_model::boardAngleOf(plane,calib.wires.wireEndpoints[start])+pi/20;
    const double r=profile.radii.back(),s=std::sin(theta),c=std::cos(theta);
    const cv::Matx33d numbered(-s/r,c/r,0,c/r,s/r,0,0,0,1);
    seed.boardToImage=plane.H*numbered;seed.imageToBoard=seed.boardToImage.inv();
    // Initial fits provide search corridors; direct image evidence supplies
    // band-centre and black/white boundary observations, never generated endpoints.
    out.observations=observe(frame,seed,{},calib.bullCenter,true);
    out.model=fit(seed,out.observations);
    return out;
}

cv::Mat overlay(const cv::Mat &frame,const Measurement &m)
{
    cv::Mat image=frame.clone();if(image.empty())return image;
    const auto &model=m.model;
    for(int k=0;k<6;++k)
    {
        std::vector<cv::Point> path;
        for(int j=0;j<=360;++j)
        {
            auto p=map(model.boardToImage,radial(j*pi/180,model.profile.radii[k]));
            if(finite(p)&&std::abs(p.x)<100000&&std::abs(p.y)<100000)path.emplace_back(cvRound(p.x),cvRound(p.y));
        }
        if(path.size()>2) cv::polylines(image,path,true,k<2?cv::Scalar(255,0,255):cv::Scalar(255,255,0),1,cv::LINE_AA);
    }
    for(int j=0;j<20;++j)
    {
        auto a=map(model.boardToImage,radial((j-0.5)*sectorAngle,model.profile.radii[1]));
        auto b=map(model.boardToImage,radial((j-0.5)*sectorAngle,model.profile.radii[5]));
        auto text=map(model.boardToImage,radial(j*sectorAngle,model.profile.radii[5]+12));
        if(finite(a)&&finite(b))cv::line(image,a,b,{200,200,0},1,cv::LINE_AA);
        if(finite(text))cv::putText(image,std::to_string(model.profile.numbers[j]),text,cv::FONT_HERSHEY_SIMPLEX,0.45,{0,255,255},1);
    }
    for(const auto &o:m.observations)
        cv::circle(image,o.image,1,o.heldOut?cv::Scalar(0,0,255):cv::Scalar(0,255,0),-1);
    cv::putText(image,model.valid?"physical model accepted":"physical model REFUSED",{12,22},cv::FONT_HERSHEY_SIMPLEX,0.55,{0,255,255},1);
    cv::putText(image,"green/red: fit/held-out landmarks; cyan: predicted scoring circles",{12,44},cv::FONT_HERSHEY_SIMPLEX,0.45,{255,255,255},1);
    return image;
}
std::string describe(const Model &m)
{
    std::ostringstream s;s.setf(std::ios::fixed);s.precision(3);
    s<<"BOARD MODEL: "<<(m.valid?"accepted":"REFUSED")<<" profile="<<m.profile.id<<"/"<<m.profile.version
     <<" train="<<m.quality.training<<" held_out="<<m.quality.heldOut<<" sectors="<<m.quality.sectors
     <<"/20 band_centres="<<m.quality.bandCentres<<" radial_wires="<<m.quality.radialWires
     <<" colour_edge_p95_mm="<<m.quality.colourEdgeP95Mm<<" rms_train_mm="<<m.quality.trainRmsMm<<" rms_held_out_mm="<<m.quality.heldOutRmsMm
     <<" p95_held_out_mm="<<m.quality.heldOutP95Mm<<" rms_held_out_px="<<m.quality.heldOutRmsPx
     <<" parameter_sigma_mm="<<m.quality.parameterSigmaMm<<" anchor_change_deg="<<m.quality.orientationErrorDeg
     <<"; "<<m.reason<<"; distortion="<<m.distortion;
    return s.str();
}
double displacementMm(const Model &before,const Model &after)
{
    if(!before.valid||!after.valid||before.imageSize!=after.imageSize||
       !sameProfile(before.profile,after.profile)) return std::numeric_limits<double>::infinity();
    double worst=0;
    for(double fraction:{0.,0.3,0.6,1.})for(int j=0;j<40;++j)
    {
        const auto p=radial(j*2*pi/40,before.profile.radii.back()*fraction);
        const auto q=map(after.imageToBoard,map(before.boardToImage,p));
        if(!finite(q))return std::numeric_limits<double>::infinity();
        worst=std::max(worst,cv::norm(q-p));
    }
    return worst;
}
std::string fingerprint(const std::vector<Model> &models)
{
    std::ostringstream s;s.precision(17);
    for(const auto &m:models)
    {
        s<<m.version<<':'<<m.valid<<':'<<m.anchored<<':'<<m.profile.id<<':'<<m.profile.version;
        for(double v:m.profile.radii)s<<':'<<v;
        for(int v:m.profile.numbers)s<<':'<<v;
        for(double v:m.boardToImage.val)s<<':'<<v;
        for(double v:m.imageToBoard.val)s<<':'<<v;
        s<<';';
    }
    return s.str();
}
void save(const std::string &path,const Model &m)
{
    // Older OpenCV FileStorage emits non-JSON escapes for apostrophes. Use the
    // application's JSON serializer; retain the matrix shape its reader accepts.
    using nlohmann::json;
    auto metric=[](double value)->json { return std::isfinite(value)?json(value):json(nullptr); };
    const auto &q=m.quality;
    json document={
        {"schema","opendartboard-physical-board"},{"version",m.version},
        {"profile",{{"id",m.profile.id},{"version",m.profile.version},{"radii_mm",m.profile.radii},
            {"numbers_clockwise",m.profile.numbers},{"rim_radius_mm",m.profile.rimRadius},
            {"boundary_tolerance_mm",m.profile.boundaryToleranceMm}}},
        {"camera_identity",m.cameraIdentity},{"width",m.imageSize.width},{"height",m.imageSize.height},
        {"valid",(int)m.valid},{"anchored",(int)m.anchored},{"anchor",m.anchorSource},
        {"coordinates","millimetres; origin=bull; +Y=20; +X=6; clockwise numbering"},
        {"board_to_image",{{"type_id","opencv-matrix"},{"rows",3},{"cols",3},{"dt","d"},
            {"data",std::vector<double>(m.boardToImage.val,m.boardToImage.val+9)}}},
        {"distortion",m.distortion},{"reason",m.reason},
        {"observed_landmarks","colour-band centres and radial black/white boundaries; scoring circles model-predicted"},
        {"quality",{{"training",q.training},{"held_out",q.heldOut},{"sectors",q.sectors},{"quadrants",q.quadrants},
            {"band_centres",q.bandCentres},{"radial_wires",q.radialWires},
            {"colour_edge_p95_mm",metric(q.colourEdgeP95Mm)},{"train_rms_mm",metric(q.trainRmsMm)},
            {"held_out_rms_mm",metric(q.heldOutRmsMm)},{"held_out_p95_mm",metric(q.heldOutP95Mm)},
            {"held_out_rms_px",metric(q.heldOutRmsPx)},{"parameter_sigma_mm",metric(q.parameterSigmaMm)},
            {"orientation_error_deg",metric(q.orientationErrorDeg)},
            {"ring_support",q.ringSupport},{"band_support",q.bandSupport}}}
    };
    std::ofstream file(path);
    if(!file || !(file<<document.dump(2)<<"\n"))throw std::runtime_error("cannot write board model: "+path);
}
Model load(const std::string &path,const std::string &identity,cv::Size size,const Profile &profile)
{
    Model m;
    try
    {
        std::ifstream file(path);if(!file)throw std::runtime_error("no board model");
        nlohmann::json document;file>>document;
        const auto schema=document.at("schema").get<std::string>();
        m.version=document.at("version").get<int>();
        m.profile=readProfile(document.at("profile"));
        m.cameraIdentity=document.at("camera_identity").get<std::string>();
        m.imageSize={document.at("width").get<int>(),document.at("height").get<int>()};
        if(schema!="opendartboard-physical-board"||m.version!=1||m.cameraIdentity!=identity||m.imageSize!=size||!sameProfile(m.profile,profile))
            throw std::runtime_error("board model identity, mode, schema or profile changed");
        const auto &matrix=document.at("board_to_image");
        if(matrix.at("rows")!=3||matrix.at("cols")!=3||matrix.at("dt")!="d"||matrix.at("data").size()!=9)
            throw std::runtime_error("invalid cached mapping");
        for(int i=0;i<9;++i)m.boardToImage.val[i]=matrix.at("data").at(i).get<double>();
        if(!cv::checkRange(cv::Mat(m.boardToImage)))throw std::runtime_error("non-finite cached mapping");
        bool ok=false;m.imageToBoard=m.boardToImage.inv(cv::DECOMP_LU,&ok);
        if(!ok)throw std::runtime_error("singular cached mapping");
        // Persistence is evidence, not permission to score on a previous board position.
        // Every start remeasures the current frame, even with --reuse-calibration.
        m.valid=false;m.anchored=false;m.reason="persisted model requires fresh image/anchor validation";
    }
    catch(const std::exception &e){m.valid=false;m.reason=e.what();}
    return m;
}
}
