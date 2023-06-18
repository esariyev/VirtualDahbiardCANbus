#include <Windows.h>    
#include <iostream>
#include <time.h>
#include <string>
#include <stdio.h>
#include <stdlib.h>
#include <opencv2/opencv.hpp>
#include "opencv2/highgui/highgui.hpp"
#include <thread>
#pragma warning(disable : 4996)

using namespace std;
using namespace cv;

uint64_t dataCAN;
 

void print_error(const char* context)
{
    DWORD error_code = GetLastError();
    char buffer[256];
    DWORD size = FormatMessageA(
        FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_MAX_WIDTH_MASK,
        NULL, error_code, MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
        buffer, sizeof(buffer), NULL);
    if (size == 0) { buffer[0] = 0; }
    fprintf(stderr, "%s: %s\n", context, buffer);
}

HANDLE open_serial_port(const char* device, uint32_t baud_rate)
{
    HANDLE port = CreateFileA(device, GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (port == INVALID_HANDLE_VALUE)
    {
        print_error(device);
        return INVALID_HANDLE_VALUE;
    }

    // Flush away any bytes previously read or written.
    BOOL success = FlushFileBuffers(port);
    if (!success)
    {
        print_error("Failed to flush serial port");
        CloseHandle(port);
        return INVALID_HANDLE_VALUE;
    }

    // Configure read and write operations to time out after 100 ms.
    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = 0;
    timeouts.ReadTotalTimeoutConstant = 100;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 0;

    success = SetCommTimeouts(port, &timeouts);
    if (!success)
    {
        print_error("Failed to set serial timeouts");
        CloseHandle(port);
        return INVALID_HANDLE_VALUE;
    }

    // Set the baud rate and other options.
    DCB state = { 0 };
    state.DCBlength = sizeof(DCB);
    state.BaudRate = baud_rate;
    state.ByteSize = 8;
    state.Parity = NOPARITY;
    state.StopBits = ONESTOPBIT;
    success = SetCommState(port, &state);
    if (!success)
    {
        print_error("Failed to set serial settings");
        CloseHandle(port);
        return INVALID_HANDLE_VALUE;
    }

    return port;
}

int write_port(HANDLE port, uint8_t* buffer, size_t size)
{
    DWORD written;
    BOOL success = WriteFile(port, buffer, size, &written, NULL);
    if (!success)
    {
        print_error("Failed to write to port");
        return -1;
    }
    if (written != size)
    {
        print_error("Failed to write all bytes to port");
        return -1;
    }
    return 0;
}

SSIZE_T read_port(HANDLE port, uint64_t* buffer, size_t size)
{
    DWORD received;
    BOOL success = ReadFile(port, buffer, size, &received, NULL);
    if (!success)
    {
        print_error("Failed to read from port");
        return -1;
    }
    return received;
}
HANDLE port = open_serial_port("\\\\.\\COM5", 9600);
const std::string number(int num)
{
    char buf[80];
    sprintf(buf, "%d", num);

    return buf;
}

const std::string currentDate() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);

    strftime(buf, sizeof(buf), "%d.%m.%Y", &tstruct);

    return buf;
}

const std::string currentTime() {
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);

    strftime(buf, sizeof(buf), "%H:%M", &tstruct);

    return buf;
}

void overlayImage(const cv::Mat& background, const cv::Mat& min, cv::Mat& output, cv::Point2i location)
{
    background.copyTo(output);


    // start at the row indicated by location, or at row 0 if location.y is negative.
    for (int y = std::max(location.y, 0); y < background.rows; ++y)
    {
        int fY = y - location.y; // because of the translation

        // we are done of we have processed all rows of the min image.
        if (fY >= min.rows)
            break;

        // start at the column indicated by location, 

        // or at column 0 if location.x is negative.
        for (int x = std::max(location.x, 0); x < background.cols; ++x)
        {
            int fX = x - location.x; // because of the translation.

            // we are done with this row if the column is outside of the min image.
            if (fX >= min.cols)
                break;

            // determine the opacity of the foregrond pixel, using its fourth (alpha) channel.
            double opacity =
                ((double)min.data[fY * min.step + fX * min.channels() + 3])

                / 255.;


            // and now combine the background and min pixel, using the opacity, 

            // but only if opacity > 0.
            for (int c = 0; opacity > 0 && c < output.channels(); ++c)
            {
                unsigned char minPx =
                    min.data[fY * min.step + fX * min.channels() + c];
                unsigned char backgroundPx =
                    background.data[y * background.step + x * background.channels() + c];
                output.data[y * output.step + output.channels() * x + c] =
                    backgroundPx * (1. - opacity) + minPx * opacity;
            }
        }
    }
}

void rotateNeedle(const cv::Mat& background, const cv::Mat& min, cv::Mat& output, double angle, cv::Point2i location)
{
    // get the center coordinates of the image to create the 2D rotation matrix
    Point2f center((min.cols - 1) / 2.0, (min.rows - 1) / 2.0);

    // using getRotationMatrix2D() to get the rotation matrix
    Mat rotation_matix = getRotationMatrix2D(center, angle, 1.0);

    // we will save the resulting image in rotated_image matrix
    Mat rotated_image;

    // rotate the image using warpAffine
    warpAffine(min, rotated_image, rotation_matix, min.size());

    // overlayImage rotated image
    overlayImage(background, rotated_image, output, location);
}

void barGraph(double val, bool state, double red, double yellow, const cv::Mat& background, cv::Mat& output, cv::Point2i location)
{
    // read images
    cv::Mat img1 = imread("pix_w.png", -1);
    cv::Mat img2 = imread("pix_r.png", -1);
    cv::Mat img3 = imread("pix_y.png", -1);
    cv::Mat img4 = imread("pix_b.png", -1);

    // boundry values
    if (state == 1)
    {
        if (val > red)
        {
            overlayImage(img4, img2, img4, cv::Point(val, 0));
        }
        else if (val > yellow)
        {
            overlayImage(img4, img3, img4, cv::Point(val, 0));
        }
        else
        {
            overlayImage(img4, img1, img4, cv::Point(val, 0));
        }
    }
    else
    {

        if (val > red)
        {
            overlayImage(img4, img1, img4, cv::Point(val, 0));
        }
        else if (val > yellow)
        {
            overlayImage(img4, img3, img4, cv::Point(val, 0));
        }
        else
        {
            overlayImage(img4, img2, img4, cv::Point(val, 0));
        }
    }

    // define croping area
    cv::Rect myROI(0, 0, 153, 9);

    // crop image
    cv::Mat croppedImage = img4(myROI);

    // overlay Image
    overlayImage(background, croppedImage, output, location);

}

int mapValue(int x, int in_min, int in_max, int out_min, int out_max)
{
    int val = ((x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min);

    return val;
}

void cluster()
{
    /******************************* Images **********************************/

    cv::Mat abs = imread("abs.png", -1);
    cv::Mat airbag = imread("airbag.png", -1);
    cv::Mat bat = imread("bat.png", -1);
    cv::Mat belt = imread("belt.png", -1);
    cv::Mat check = imread("check.png", -1);
    cv::Mat cruise = imread("cruise.png", -1);
    cv::Mat door = imread("door.png", -1);
    cv::Mat esp = imread("esp.png", -1);
    cv::Mat fuel = imread("fuel.png", -1);
    cv::Mat frame = imread("frame.png", -1);
    cv::Mat high = imread("high.png", -1);
    cv::Mat low = imread("low.png", -1);
    cv::Mat lefth = imread("lefth.png", -1);
    cv::Mat needle = imread("needle.png", -1);
    cv::Mat oil = imread("oil.png", -1);
    cv::Mat parking = imread("parking.png", -1);
    cv::Mat pedal = imread("pedal.png", -1);
    cv::Mat rigth = imread("rigth.png", -1);
    cv::Mat radar = imread("radar.png", -1);
    cv::Mat temp = imread("temp.png", -1);
    cv::Mat back = imread("audi.png", -1);
    cv::Mat output;

    for (;;)
    {
        /************************** Extract CAN bytes ****************************/

        int speed = ((dataCAN >> 32) & 0xFF);
        int tacho = (((dataCAN >> 24) & 0xFF) << 8) | (((dataCAN >> 16) & 0xFF));
        int ful = ((dataCAN >> 40) & 0xFF);
        int tmp = ((dataCAN >> 48) & 0xFF);
        int gear = ((dataCAN >> 56) & 0xFF);

        /*********************** Get Sensor Value ******************************/

        int cels = mapValue(tmp, 50, 130, -306, -150);
        int litr = mapValue(ful, 0, 70, -306, -150);
        int spd = mapValue(speed, 0, 200, 142, -136);
        int rpm = mapValue(tacho, 0, 8000, 133, -130);

        /*********************** Fuel and Temperature **************************/

        // temerature graph
        barGraph(cels, 1, -190, -210, frame, output, cv::Point(252, 701));

        // fuel graph
        barGraph(litr, 0, -270, -280, output, output, cv::Point(872, 701));

        // create frame
        overlayImage(output, back, output, cv::Point(0, 0));

        /************************ On-Board Computer ***************************/

        //  clock
        Point cl_org(505, 645);
        putText(output, currentTime(), cl_org, FONT_HERSHEY_SCRIPT_SIMPLEX, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        //  date
        Point dt_org(575, 270);
        putText(output, currentDate(), dt_org, FONT_HERSHEY_SCRIPT_SIMPLEX, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        //  general trip 
        Point gt_org(500, 615);
        putText(output, "1245.0 km", gt_org, FONT_ITALIC, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        //  local trip 
        Point lt_org(715, 615);
        putText(output, "20.0 km", lt_org, FONT_ITALIC, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        //  outside temperature
        Point ot_org(715, 645);
        putText(output, "24.0 C", ot_org, FONT_ITALIC, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        // fuel consuption menu
        Point f_org(540, 355);
        putText(output, "Fuel consuption", f_org, FONT_ITALIC, 0.8, Scalar(255, 255, 255), 2, LINE_4);

        /***************** Speedometer and Tachometer ***********************/

        // rpm
        rotateNeedle(output, needle, output, rpm, cv::Point(85, 250));

        // speed
        rotateNeedle(output, needle, output, spd, cv::Point(815, 250));

        if (speed > 0 && speed < 10)
        {
            Point s_org(993, 455);
            putText(output, number(speed), s_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 5, LINE_4);
        }
        else if (speed > 9 && speed < 100)
        {
            Point s_org(970, 455);
            putText(output, number(speed), s_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 5, LINE_4);
        }
        else if (speed > 100)
        {
            Point s_org(950, 455);
            putText(output, number(speed), s_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 5, LINE_4);
        }

        /****************************** Gear ******************************/

        // gear
        if (gear == 0)
        {
            Point g_org(257, 460);
            putText(output, "P", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 1)
        {
            Point g_org(257, 460);
            putText(output, "R", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 2)
        {
            Point g_org(257, 460);
            putText(output, "N", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 3)
        {
            Point g_org(257, 460);
            putText(output, "D", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 4)
        {
            Point g_org(240, 460);
            putText(output, "D1", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 5)
        {
            Point g_org(240, 460);
            putText(output, "D2", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 6)
        {
            Point g_org(240, 460);
            putText(output, "D3", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 7)
        {
            Point g_org(240, 460);
            putText(output, "D4", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 8)
        {
            Point g_org(240, 460);
            putText(output, "D5", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 9)
        {
            Point g_org(240, 460);
            putText(output, "D6", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }
        else if (gear == 10)
        {
            Point g_org(240, 460);
            putText(output, "D7", g_org, FONT_ITALIC, 2.0, Scalar(255, 255, 255), 6, LINE_4);
        }

        /*********************** Warning Lamp ****************************/

        // lefth
        if (dataCAN & (1 << 0))
        {
            overlayImage(output, lefth, output, cv::Point(428, 130));
        }
        // rigth
        if (dataCAN & (1 << 1))
        {
            overlayImage(output, rigth, output, cv::Point(810, 128));
        }
        // check
        if (dataCAN & (1 << 2))
        {
            overlayImage(output, check, output, cv::Point(642, 130));
        }
        // airbag
        if (dataCAN & (1 << 3))
        {
            overlayImage(output, airbag, output, cv::Point(700, 137));
        }
        // low
        if (dataCAN & (1 << 4))
        {
            overlayImage(output, low, output, cv::Point(750, 138));
        }
        // high
        if (dataCAN & (1 << 5))
        {
            overlayImage(output, high, output, cv::Point(590, 137));
        }
        // belt
        if (dataCAN & (1 << 6))
        {
            overlayImage(output, belt, output, cv::Point(537, 139));
        }
        // esp
        if (dataCAN & (1 << 7))
        {
            overlayImage(output, esp, output, cv::Point(490, 139));
        }
        // abs
        if (dataCAN & (1 << 8))
        {
            overlayImage(output, abs, output, cv::Point(488, 713));
        }
        // bat
        if (dataCAN & (1 << 9))
        {
            overlayImage(output, bat, output, cv::Point(543, 710));
        }
        // oil
        if (dataCAN & (1 << 10))
        {
            overlayImage(output, oil, output, cv::Point(599, 710));
        }
        // temp
        if (dataCAN & (1 << 11))
        {
            overlayImage(output, temp, output, cv::Point(430, 708));
        }
        // cruise
        if (dataCAN & (1 << 12))
        {
            overlayImage(output, cruise, output, cv::Point(722, 710));
        }
        // parking
        if (dataCAN & (1 << 13))
        {
            overlayImage(output, parking, output, cv::Point(772, 713));
        }
        // fuel
        if (dataCAN & (1 << 14))
        {
            overlayImage(output, fuel, output, cv::Point(818, 712));
        }
        // radar
        if (dataCAN & (1 << 15))
        {
            overlayImage(output, radar, output, cv::Point(673, 712));
        }

        // display 
        cv::imshow("Virtual Cluster", output);

        // wait
        waitKey(1);

    }
}

void test()
{
    uint64_t receive[1];
     
    for (;;)
    {   
        read_port(port, receive, 8);
        dataCAN = receive[0];
    }

    

 

}

 

int main(int, char**)
{   
    std::thread first(cluster);
    std::thread second(test);
    first.join();
    second.join();
}

