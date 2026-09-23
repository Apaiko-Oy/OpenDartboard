#include "detector/geometry/calibration/board_model.hpp"
#include <iostream>
#include <cmath>
#include <filesystem>
#include <cstdlib>
using namespace board_model;
static int failures=0;
void check(bool ok,const std::string &name){std::cout<<(ok?"OK ":"FAIL ")<<name<<"\n";if(!ok)++failures;}
static cv::Point2d at(double a,double r){return {r*std::sin(a),r*std::cos(a)};}
static Model known()
{
    Model m;m.anchored=true;m.imageSize={1280,720};m.anchorSource="synthetic independent numbered landmark";
    m.boardToImage={1.8,0.12,640,0.15,-1.25,350,0.0008,-0.0012,1};
    m.imageToBoard=m.boardToImage.inv();m.cameraIdentity="club's-camera-a:1280x720:30";return m;
}
static std::vector<Observation> points(const Model &m,int first=0,int last=240,double distortion=0)
{
    std::vector<Observation> obs;
    for(int j=first;j<last;++j)
    {
        double a=(j+0.5)*2*CV_PI/240;
        int sector=(int)std::floor((a+CV_PI/20)/(CV_PI/10))%20;
        for(int k=2;k<6;++k)
        {
            auto p=map(m.boardToImage,at(a,m.profile.radii[k]));
            auto offset=p-cv::Point2d(640,360);
            p+=offset*(distortion*offset.dot(offset)/(300*300));
            // Deterministic subpixel noise, independent of the initial mapping.
            p.x+=0.15*std::sin(j*2.1+k);p.y+=0.15*std::cos(j*1.7+k);
            obs.push_back({p,Kind::Ring,m.profile.radii[k],k,sector,(j/2)%3==0});
        }
    }
    for(int j=0;j<20;++j)
    {
        double a=(j-0.5)*CV_PI/10;
        for(double r:{70.,120.,160.})obs.push_back({map(m.boardToImage,at(a,r)),Kind::Wire,a,-1,j,j%3==0});
    }
    return obs;
}
int main(int argc,char **argv)
{
    Model truth=known(),seed=truth;seed.boardToImage(0,2)+=3;seed.boardToImage(1,2)-=2;seed.imageToBoard=seed.boardToImage.inv();
    auto singular=seed;singular.boardToImage=cv::Matx33d::zeros();
    check(!fit(singular,points(truth)).valid,"singular initial mapping refused");
    auto obs=points(truth);auto m=fit(seed,obs);std::cout<<describe(m)<<"\n";
    check(m.valid,"known perspective fit accepted");
    double maxError=0;
    // Independent surface positions, not the training radii or candidate endpoints.
    for(double radius:{25.,53.,83.,125.,150.,169.})for(int j=0;j<37;++j)
    {
        auto p=at(j*2*CV_PI/37,radius);auto recovered=map(m.imageToBoard,map(truth.boardToImage,p));
        maxError=std::max(maxError,cv::norm(p-recovered));
    }
    check(maxError<0.5,"independent surface positions recover within 0.5 mm");
    cv::Mat front(480,480,CV_8U,cv::Scalar(0));
    for(int pair=1;pair>=0;--pair)
    {
        int inner=pair==0?2:4;
        cv::circle(front,{240,240},cvRound(truth.profile.radii[inner+1]),cv::Scalar(255),-1);
        cv::circle(front,{240,240},cvRound(truth.profile.radii[inner]),cv::Scalar(0),-1);
    }
    // Draw independently, then warp to a camera. No generated correspondence is
    // passed to observe(); it must find the image's colour crossings.
    cv::Mat camera;
    cv::Matx33d pixelsToBoard(1,0,-240,0,-1,240,0,0,1);
    cv::warpPerspective(front,camera,cv::Mat(truth.boardToImage*pixelsToBoard),truth.imageSize);
    std::vector<cv::Point2f> observedWires;
    for(const auto &o:obs) if(o.kind==Kind::Wire) observedWires.push_back(o.image);
    auto extracted=observe(camera,seed,observedWires,cv::Point2d(),false);
    auto imageFit=fit(seed,extracted);
    check(imageFit.valid,"independently rendered colour edges support the model");
    // Independent colour rendering: red fringes expand both sides by 2 mm.
    // The band centre remains a landmark; treating either fringe as a scoring
    // boundary would systematically widen every red treble/double.
    cv::Mat colourFront(960,960,CV_8UC3);
    for(int y=0;y<960;++y)for(int x=0;x<960;++x)
    {
        cv::Point2d p((x-480)*0.5,(480-y)*0.5);double r=cv::norm(p);
        int sector=((int)std::floor((std::atan2(p.x,p.y)+CV_PI/20)/(CV_PI/10))%20+20)%20;
        bool red=sector%2==0;cv::Vec3b c=red?cv::Vec3b(35,35,35):cv::Vec3b(220,230,230);
        if(r>175)c={35,35,35};
        for(int ring:{2,4})
        {
            double fringe=red?2.:0.;
            if(r>=truth.profile.radii[ring]-fringe && r<=truth.profile.radii[ring+1]+fringe)
                c=red?cv::Vec3b(100,130,230):cv::Vec3b(80,180,100);
        }
        colourFront.at<cv::Vec3b>(y,x)=c;
    }
    cv::Mat coloured;
    cv::Matx33d colourPixelsToBoard(.5,0,-240,0,-.5,240,0,0,1);
    cv::warpPerspective(colourFront,coloured,cv::Mat(truth.boardToImage*colourPixelsToBoard),truth.imageSize);
    auto colourObs=observe(coloured,seed,{},cv::Point2d(),false);
    auto colourFit=fit(seed,colourObs);
    check(colourFit.valid,"colour-band centres survive symmetric colour fringes");
    double colourError=0;
    for(double r:{53.,99.,107.,150.,162.,170.})for(int j=0;j<37;++j)
    {
        auto p=at(j*2*CV_PI/37,r);
        colourError=std::max(colourError,cv::norm(map(colourFit.imageToBoard,map(truth.boardToImage,p))-p));
    }
    check(colourError<1.0,"independent scoring boundaries recover despite widened red colour bands");
    check(colourFit.quality.colourEdgeP95Mm>colourFit.quality.heldOutP95Mm,
          "raw colour-fringe error stays visible separately from landmark fit error");
    auto partial=fit(seed,points(truth,0,180));check(partial.valid,"three quadrants constrain a partial fit without a bull");
    auto sparse=fit(seed,points(truth,0,30));check(!sparse.valid,"small supported arc refused");
    auto unanchored=seed;unanchored.anchored=false;check(!fit(unanchored,obs).valid,"unresolved numbered rotation refused");
    auto wrong=seed;double a=CV_PI/10;cv::Matx33d R(std::cos(a),-std::sin(a),0,std::sin(a),std::cos(a),0,0,0,1);
    wrong.boardToImage=truth.boardToImage*R;wrong.imageToBoard=wrong.boardToImage.inv();
    check(!fit(wrong,obs).valid,"one-sector rotation contradicting independent anchor refused");
    auto mirror=truth;mirror.boardToImage=truth.boardToImage*cv::Matx33d(-1,0,0,0,1,0,0,0,1);mirror.imageToBoard=mirror.boardToImage.inv();
    check(!fit(mirror,points(mirror)).valid,"mirrored handedness refused");
    check(!fit(seed,points(truth,0,240,0.25)).valid,"unmodelled radial distortion refused");
    auto falseRings=obs;for(auto &o:falseRings)if(o.kind==Kind::Ring&&o.ring<4)o.target*=0.8;
    check(!fit(seed,falseRings).valid,"wrong treble identity refused");
    auto contaminated=obs;for(auto &o:contaminated)if(o.heldOut)o.image.x+=15;
    check(!fit(seed,contaminated).valid,"held-out observations affect acceptance, never disappear into fitting");
    auto other=truth;other.profile.id="another-club-board";check(fit(other,obs).valid,"another named board with same nominal scoring geometry is supported");
    check(score(m,map(truth.boardToImage,{0,103}))=="T20","treble is scored from physical radius");
    check(score(m,map(truth.boardToImage,{0,166}))=="D20","double is scored from physical radius");
    check(score(m,map(truth.boardToImage,{0,180}))=="MISS","outside board is MISS");
    check(score(m,map(truth.boardToImage,{0,10}))=="OUTER","outer bull stays distinct from MISS");
    const auto sealed=fingerprint({m});auto changedModel=m;changedModel.imageToBoard(0,2)+=1;
    check(fingerprint({changedModel})!=sealed,"geometry seal includes the inverse scoring mapping");
    check(displacementMm(m,m)<1e-8,"unchanged physical geometry agrees on recovery");
    auto moved=m;moved.boardToImage=cv::Matx33d(1,0,8,0,1,0,0,0,1)*m.boardToImage;
    moved.imageToBoard=moved.boardToImage.inv();
    check(displacementMm(m,moved)>m.profile.boundaryToleranceMm,"camera movement invalidates physical geometry on recovery");
    auto rotated=m;rotated.boardToImage=m.boardToImage*R;rotated.imageToBoard=rotated.boardToImage.inv();
    check(displacementMm(m,rotated)>m.profile.boundaryToleranceMm,"numbered board rotation invalidates physical geometry");
    auto unreadable=m;unreadable.valid=false;
    check(!std::isfinite(displacementMm(m,unreadable)),"unreadable recovery cannot certify old geometry");
    if(argc>1)
    {
        std::string p=std::string(argv[1])+"/model.json";std::filesystem::create_directories(argv[1]);save(p,m);
        save(std::string(argv[1])+"/refused.json",Model());
        auto stored=load(p,m.cameraIdentity,m.imageSize,m.profile);
        check(!stored.valid&&stored.reason.find("fresh")!=std::string::npos,"persisted mapping requires current-frame validation");
        check(cv::norm(cv::Mat(stored.boardToImage),cv::Mat(m.boardToImage))<1e-9,"mapping round trips without raw-cache ABI changes");
        check(load(p,"swapped-camera",m.imageSize,m.profile).reason.find("changed")!=std::string::npos,"camera identity change invalidates persistence");
        check(load(p,m.cameraIdentity,{640,480},m.profile).reason.find("changed")!=std::string::npos,"capture mode change invalidates persistence");
        auto changed=m.profile;changed.radii[2]+=1;
        check(load(p,m.cameraIdentity,m.imageSize,changed).reason.find("changed")!=std::string::npos,"profile contents change invalidates persistence even with same name");
    }
    std::cout<<"failures="<<failures<<"\n";return failures?1:0;
}
