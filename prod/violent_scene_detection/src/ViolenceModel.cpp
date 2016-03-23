/*
 * ViolenceModel.cpp
 *
 *  Created on: Mar 16, 2016
 *      Author: josephcarson
 */

#include <iostream>
#include <unistd.h>
#include <ejdb/ejdb.h>

#include "ImageBlob.h"
#include "ViolenceModel.h"
#include "ImageUtil.h"

#include <boost/heap/priority_queue.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>

#include <opencv2/opencv.hpp>


#define VIOLENCE_MODEL_DB_NAME "violence_model.db"
#define VIOLENCE_MODEL_TRAINING_SET "violence_model_train"

// This is just a suitable default for now.  Eventually, this should be made configurable.
const uint GRACIA_K = 8;

ViolenceModel::ViolenceModel(std::string trainingStorePath)
: ejdb(NULL),
  trainingStorePath(trainingStorePath)
{
	ejdbInit();
	trainingStoreInit();
}

void ViolenceModel::trainingStoreInit()
{
	cv::FileStorage file;
	bool trainingStoreOpenSuccess = file.open(trainingStorePath, cv::FileStorage::READ | cv::FileStorage::WRITE);
	if (!trainingStoreOpenSuccess) {
		std::cout << "Failed opening training store at " << trainingStorePath << "\n";
		return;
	}

	file[VIOLENCE_MODEL_TRAINING_SET] >> trainingStore;
	std::cout << "trainingStore is " << trainingStore << "\n";
}

void ViolenceModel::ejdbInit()
{
	EJDB * tempEJDBPtr = NULL;

	if ( !ejdb ) {

		tempEJDBPtr = ejdbnew();

		if ( !tempEJDBPtr ) {
			std::cerr << "Failed Instantiating EJDB object.";
		} else if ( !ejdbopen(tempEJDBPtr, VIOLENCE_MODEL_DB_NAME, JBOWRITER | JBOCREAT) ) {
			std::cerr << "Failed opening EJDB database.";
			free(tempEJDBPtr); tempEJDBPtr = NULL;
		} else {
			// Success.
			std::cout << "Database opened.";
			this->ejdb = tempEJDBPtr;
		}
	}
}



void ViolenceModel::index(std::string resourcePath)
{
	// Create a VideoCapture instance bound to the path.
	cv::VideoCapture capture(resourcePath);
	cv::Mat currentFrame, prevFrame;

	// Create a max heap for keeping track of the largest blobs.
	boost::heap::priority_queue<ImageBlob> topBlobsHeap;
	topBlobsHeap.reserve(GRACIA_K);

	bool capPrevSuccess = false;
	bool capCurrSuccess = false;

	// Load the prev frame with the first frame and current with the second
	// so that we can simply loop and compute.
	for ( uint i = 0, capPrevSuccess = capture.read(prevFrame), capCurrSuccess = capture.read(currentFrame);
		  capPrevSuccess && capCurrSuccess;
		  prevFrame = currentFrame, capCurrSuccess = capture.read(currentFrame), i++ )
	{
		std::cout<< "frame: " << i << "\n";

		// Convert to grayscale.
		if ( i == 0 ) {
			cv::Mat grayOut;
			// It's only necessary to gray scale filter the previous frame on the first iteration,
			// as each time the current frame will be equal to the prev frame, which was already filtered.
			cv::cvtColor(prevFrame, grayOut, CV_RGB2GRAY);
			prevFrame = grayOut;
		}

		// Filter the current frame.
		cv::Mat currentOut;
		cv::cvtColor(currentFrame, currentOut, CV_RGB2GRAY);
		currentFrame = currentOut;

		// Compute absolute binarized difference.
		cv::Mat absDiff, binAbsDiff;
		cv::absdiff(prevFrame, currentFrame, absDiff);
		cv::threshold ( absDiff, binAbsDiff, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU );

		// Output binAbsDiff for debug purposes.
		boost::filesystem::path bpath(resourcePath);
		std::stringstream frameName;
		frameName << "bin_abs_diff_" << bpath.stem().string() << "_" << i;
		ImageUtil::dumpDebugImage(binAbsDiff, frameName.str());

		// Find the contours (blobs) and use them to compute centroids, area, etc.
		// http://opencv.itseez.com/2.4/doc/tutorials/imgproc/shapedescriptors/moments/moments.html?highlight=moment#code
		std::vector<std::vector<cv::Point>> contours;
		std::vector<cv::Vec4i> hierarchy;
		cv::findContours(binAbsDiff, contours, hierarchy, CV_RETR_LIST, CV_CHAIN_APPROX_NONE);

		// TODO: At the moment we're just reading these values into local variables.  The intention is to store them
		// in a class member that can store it in a format that is both efficient for learning model computation and
		// persistent store in the file system (so that the arduous process of extracting the features isn't necessary
		// every single time.
		// See http://www.boost.org/doc/libs/1_39_0/libs/bimap/doc/html/boost_bimap/one_minute_tutorial.html
		BOOST_FOREACH(std::vector<cv::Point> cont, contours)
		{
			ImageBlob blob(cont);

			std::cout << "blob: " << blob << "\n";

			if ( topBlobsHeap.size() < GRACIA_K ) {
				// The heap isn't full yet, we can simply keep adding.
				topBlobsHeap.emplace(blob);
			} else if ( topBlobsHeap.top().area() < blob.area()) {
				// The new blob is larger and the heap is full, so bump out the top one.
				topBlobsHeap.pop();
				topBlobsHeap.emplace(blob);
			}
		}
	}
}

ViolenceModel::~ViolenceModel() {
	// TODO Auto-generated destructor stub
}

