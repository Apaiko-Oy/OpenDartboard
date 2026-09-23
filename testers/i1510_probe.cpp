#include "detector/geometry/calibration/board_model.hpp"
#include "detector/geometry/calibration/geometry_calibration.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
int main(int argc,char **argv)
{
    if(argc<4){std::cerr<<"usage: probe output-dir start-ms camera...\n";return 2;}
    std::filesystem::create_directories(argv[1]);
    int refused=0;
    for(int i=3;i<argc;++i)
    {
        cv::VideoCapture cap(argv[i]);cap.set(cv::CAP_PROP_POS_MSEC,std::atof(argv[2]));
        cv::Mat frame,sum;
        for(int j=0;j<30;++j){cv::Mat f,fl;if(!cap.read(f))return 2;f.convertTo(fl,CV_32F);if(sum.empty())sum=fl;else sum+=fl;}
        sum.convertTo(frame,CV_8U,1./30);
        auto initial=geometry_calibration::calibrateSingleCamera(frame,i-3,false);
        auto measured=board_model::measure(frame,initial);
        if(measured.observations.empty())
        {std::cerr<<"FAIL no observable board evidence for camera "<<i-2<<"\n";return 2;}
        measured.model.cameraIdentity=std::string(argv[i])+":"+std::to_string(frame.cols)+"x"+std::to_string(frame.rows);
        std::cout<<"camera="<<i-2<<" "<<board_model::describe(measured.model)<<"\n";
        const auto base=std::string(argv[1])+"/camera-"+std::to_string(i-2);
        cv::imwrite(base+".jpg",board_model::overlay(frame,measured));
        board_model::save(base+".json",measured.model);
        std::ofstream csv(base+".csv");csv<<"kind,ring,sector,held_out,x,y,target,error_mm\n";
        for(const auto &o:measured.observations)
        {
            auto p=board_model::map(measured.model.imageToBoard,o.image);
            double error=(o.kind==board_model::Kind::Ring || o.kind==board_model::Kind::BandCentre)?cv::norm(p)-o.target:
                (o.kind==board_model::Kind::Wire?p.x*std::cos(o.target)-p.y*std::sin(o.target):(o.target==0?p.x:p.y));
            csv<<(int)o.kind<<","<<o.ring<<","<<o.sector<<","<<o.heldOut<<","<<o.image.x<<","<<o.image.y<<","<<o.target<<","<<error<<"\n";
        }
        if(!measured.model.valid)++refused;
    }
    std::cout<<"physical_model_refused="<<refused<<"\n";
    const char *required=std::getenv("OD_1510_REQUIRE_ACCEPTED");
    // Measurement mode reports refused fits too. Acceptance mode is a separate gate.
    return required && std::string(required)=="1" && refused ? 1 : 0;
}
