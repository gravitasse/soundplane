
// Part of the Soundplane client software by Madrona Labs.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#include "TouchTracker.h"

std::ostream& operator<< (std::ostream& out, const Touch & t)
{
	out << std::setprecision(4);
	out << "[" << t.x << ", " << t.y << ", " << t.zf << " (" << t.age << ")]";
	return out;
}

Touch::Touch() : 
	x(0), y(0), z(0), dz(0), zf(0), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}

Touch::Touch(float px, float py, float pz, float pdz) : 
	x(px), y(py), z(pz), dz(pdz), zf(0.), zf10(0.), dzf(0.), 
	key(-1), age(0), retrig(0), releaseCtr(0)
{
}

TouchTracker::TouchTracker(int w, int h) :
	mWidth(w),
	mHeight(h),
	mpIn(0),
	mpInputMap(0),
	mNumNewCentroids(0),
	mNumCurrentCentroids(0),
	mNumPreviousCentroids(0),
	mMatchDistance(2.0f),
	mNumPeaks(0),
	mOnThreshold(0.03f),
	mOffThreshold(0.02f),
	mTaxelsThresh(9),
	mQuantizeToKey(false),
	mTemplateThresh(0.003),
	mOverrideThresh(0.01),
	mCombineRadius(1.),
	mCount(0),
	mTouchesPerFrame(0),
	mBackgroundFilter(1, 1),
	mNeedsClear(true),
	mKeyboardType(rectangularA),
	mCalibrator(w, h),
	mSampleRate(1000.f),
	mBackgroundFilterFreq(0.125f)
{
	mTouches.resize(kTrackerMaxTouches);	
	mTouchesToSort.resize(kTrackerMaxTouches);	

	// TODO checks
	if(mpInputMap) 
	{
		delete[] mpInputMap;
		mpInputMap = 0;
	}
	mpInputMap = new e_pixdata[w*h];
	
	mTestSignal.setDims(w, h);
	mCalibratedSignal.setDims(w, h);
	mCookedSignal.setDims(w, h);
	mBackground.setDims(w, h);
	mBackgroundFilterFrequency.setDims(w, h);
	mBackgroundFilterFrequency2.setDims(w, h);
	mFilteredInput.setDims(w, h);
	mSumOfTouches.setDims(w, h);
	mInhibitMask.setDims(w, h);
	mTemplateMask.setDims(w, h);
	mInputMinusBackground.setDims(w, h);
	mCalibrationProgressSignal.setDims(w, h);
	mResidual.setDims(w, h);
	mFilteredResidual.setDims(w, h);
	mTemp.setDims(w, h);
	mRetrigTimer.setDims(w, h);
	mDzSignal.setDims(w, h);
	mBackgroundFilter.setDims(w, h);
	mBackgroundFilter.setSampleRate(mSampleRate);
	mTemplateScaled.setDims (kTemplateSize, kTemplateSize);

	mNumKeys = 150; // Soundplane A
	mKeyStates.resize(mNumKeys);
	for(int i=0; i<mNumKeys; ++i)
	{
		mKeyStates[i].mKeyCenter = getKeyCenterByIndex(i);
	}
}
		
TouchTracker::~TouchTracker()
{
	if(mpInputMap) delete[] mpInputMap;
}

void TouchTracker::setInputSignal(MLSignal* pIn)
{ 
	mpIn = pIn; 

}

void TouchTracker::setOutputSignal(MLSignal* pOut)
{ 
	mpOut = pOut; 
	int w = pOut->getWidth();
	int h = pOut->getHeight();
	
	if (w < 5)
	{
		debug() << "TouchTracker: output signal too narrow!\n";
		return;
	}
	if (h < mTouchesPerFrame)
	{
		debug() << "error: TouchTracker: output signal too short to contain touches!\n";
		return;
	}
}

void TouchTracker::setMaxTouches(int t)
{
	int newT = clamp(t, 0, kTrackerMaxTouches);
	if(newT != mTouchesPerFrame)
	{
		mTouchesPerFrame = newT;
	}
}

// return the index of the key over the point p.
//
int TouchTracker::getKeyIndexAtPoint(const Vec2 p) 
{
	int k = -1;
	float x = p.x();
	float y = p.y();
	int ix, iy;
	switch (mKeyboardType)
	{
		case(rectangularA):
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 29.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 4.f);
			iy = ky;

			k = iy*30 + ix;
			break;
	}
	return k;
}


// return the center of the key over the point p.
//
Vec2 TouchTracker::getKeyCenterAtPoint(const Vec2 p) 
{
	float x = p.x();
	float y = p.y();
	int ix, iy;
	float fx, fy;
	switch (mKeyboardType)
	{
		case(rectangularA):
		default:
			MLRange xRange(3.5f, 59.5f);
			xRange.convertTo(MLRange(1.f, 29.f));
			float kx = xRange(x);
			kx = clamp(kx, 0.f, 30.f);
			ix = kx;
			
			MLRange yRange(1.25, 5.75);  // Soundplane A as measured
			yRange.convertTo(MLRange(1.f, 4.f));
			float ky = yRange(y);
			ky = clamp(ky, 0.f, 5.f);
			iy = ky;
			
			MLRange xRangeInv(1.f, 29.f);
			xRangeInv.convertTo(MLRange(3.5f, 59.5f));
			fx = xRangeInv(ix + 0.5f);
			
			MLRange yRangeInv(1.f, 4.f);
			yRangeInv.convertTo(MLRange(1.25, 5.75));
			fy = yRangeInv(iy + 0.5f);

			break;
	}
	return Vec2(fx, fy);
}

Vec2 TouchTracker::getKeyCenterByIndex(int idx) 
{
	// for Soundplane A only
	int iy = idx/30;
	int ix = idx - iy*30;
	
	MLRange xRangeInv(1.f, 29.f);
	xRangeInv.convertTo(MLRange(3.5f, 59.5f));
	float fx = xRangeInv(ix + 0.5f);

	MLRange yRangeInv(1.f, 4.f);
	yRangeInv.convertTo(MLRange(1.25, 5.75));
	float fy = yRangeInv(iy + 0.5f);

	return Vec2(fx, fy);
}

int TouchTracker::touchOccupyingKey(int k)
{
	int r = -1;
	for(int i=0; i<mTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}

int TouchTracker::getNeighborFlags(int key)
{
	// Soundplane A dims
	const int width = 30;
	const int height = 5;
	int ky = key/width;
	int kx = key - ky*width;
	bool n = 0;
	bool s = 0;
	bool e = 0;
	bool w = 0;
	bool ne = 0;
	bool nw = 0;
	bool se = 0;
	bool sw = 0;
	bool nOK = (ky < height - 1);
	bool sOK = (ky > 0);
	bool eOK = (kx < width - 1);
	bool wOK = (kx > 0);
	if(nOK)
	{
		n = keyIsOccupied(key + width);
		if(eOK)
		{
			ne = keyIsOccupied(key + width + 1);
		}
		if(wOK)
		{
			nw = keyIsOccupied(key + width - 1);
		}
	}
	if(sOK)
	{
		s = keyIsOccupied(key - width);
		if(eOK)
		{
			se = keyIsOccupied(key - width + 1);
		}
		if(wOK)
		{
			sw = keyIsOccupied(key - width - 1);
		}
	}
	if(eOK)
	{
		e = keyIsOccupied(key + 1);
	}
	if(wOK)
	{
		w = keyIsOccupied(key - 1);
	}
	
//debug() << " n: " << n << " s: " << s << " e: " << e << " w: " << w << "\n";
//debug() << " ne: " << ne << " se: " << se << " nw: " << nw << " sw: " << sw << "\n";

	return (nw<<7) + (n<<6) + (ne<<5) + (w<<4) + (e<<3) + (sw<<2) + (s<<1) + (se<<0);
}
	
// add new touch at first free slot.  TODO touch allocation modes including rotate.
// return index of new touch.
//
int TouchTracker::addTouch(const Touch& t)
{
	int newIdx = -1;
	int minIdx = 0;
	float minZ = 1.f;
	for(int j=0; j<mTouchesPerFrame; ++j)
	{
		Touch& r = mTouches[j];
		if (!r.isActive())
		{		
			r = t;
			r.age = 1;			
			r.releaseCtr = 0;			
			newIdx = j;
			return newIdx;
		}
		else
		{
			float rz = r.z;
			if (r.z < minZ)
			{
				minIdx = j;
				minZ = rz; 
			}
		}
	}
	
	// if we got here, all touches were active.
	// replace the touch with lowest z if new touch is greater.
	//
	if (t.z > minZ)
	{
		int n = mTouches.size();		
		if (n > 0)
		{
			Touch& r = mTouches[minIdx];
			r = t;
			r.age = 1;	
			r.releaseCtr = 0;		
			newIdx = minIdx;
		}		
	}
	return newIdx;
}

// return index of touch at key k, if any.
//
int TouchTracker::getTouchIndexAtKey(const int k)
{
	int r = -1;
	for(int i=0; i<mTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{		
			if(t.key == k)
			{
				r = i;
				break;
			}
		}
	}
	return r;
}

// remove the touch at index by setting the age to 0.  leave the
// position intact for downstream filters etc.  
//
void TouchTracker::removeTouchAtIndex(int touchIdx)
{
	Touch& t = mTouches[touchIdx];
	t.age = 0;
	t.key = -1;
}

void TouchTracker::clear()
{
	for (int i=0; i<mTouchesPerFrame; i++)	
	{
		mTouches[i] = Touch();	
	}
	mBackgroundFilter.clear();
	mNeedsClear = true;
}
	
void TouchTracker::setThresh(float f) 
{ 
	const float kHysteresis = 0.002f;
	mOffThreshold = f; 
	mOnThreshold = f + kHysteresis; 
	mOverrideThresh = mOnThreshold*5.f;
	mCalibrator.setThreshold(mOnThreshold);
}

// if any neighbors of the input coordinates are higher, move the coordinates there.
//
Vec2 TouchTracker::adjustPeak(const MLSignal& in, int x, int y)
{
	float t = 0.0;
	int rx = x;
	int ry = y;
	float a = in(x - 1, y - 1);
	float b = in(x, y - 1);
	float c = in(x + 1, y - 1);
	float d = in(x - 1, y);
	float e = in(x, y);
	float f = in(x + 1, y);
	float g = in(x - 1, y + 1);
	float h = in(x, y + 1);
	float i = in(x + 1, y + 1);
	float fmax = e;
	
	if (y > 0)
	{
		if (a > fmax + t)
		{
//			debug() << "<^ ";
			fmax = a; rx = x - 1; ry = y - 1;		
		}
		if (b > fmax + t)
		{
//			debug() << "^ ";
			fmax = b; rx = x; ry = y - 1;		
		}
		if (c > fmax + t)
		{
//			debug() << ">^ ";
			fmax = c; rx = x + 1; ry = y - 1;		
		}
	}
	if (d > fmax + t)
	{
//		debug() << "<- ";
		fmax = d; rx = x - 1; ry = y;		
	}
	if (f > fmax + t)
	{
//		debug() << "-> ";
		fmax = f; rx = x + 1; ry = y;		
	}
	if (y < in.getHeight() - 1)
	{
		if (g > fmax + t)
		{
//			debug() << "<_ ";
			fmax = g; rx = x - 1; ry = y + 1;		
		}
		if (h > fmax + t)
		{
//			debug() << "_ ";
			fmax = h; rx = x; ry = y + 1;		
		}
		if (i > fmax + t)
		{
//			debug() << ">_ ";
			fmax = i; rx = x + 1; ry = y + 1;		
		}
	}
	return Vec2(rx, ry);
}

// if the template matches better at any neighboring coordinates, move there.
//
Vec2 TouchTracker::adjustPeakToTemplate(const MLSignal& in, int x, int y)
{
	int xMin = x;
	int yMin = y;
	float d;
	float dMin = 100000.f;
	for(int j = -1; j < 2; ++j)
	{
		for(int i = -1; i < 2; ++i)
		{
			d = mCalibrator.differenceFromTemplateTouch(in, Vec2(x + i, y + j));
			if(d < dMin)
			{
				dMin = d;
				xMin = x + i;
				yMin = y + j;
			}
		}
	}
	return Vec2(xMin, yMin);
}
/*
// given an integer position and signal, return the peak of
// the smoothed signal using 2nd order Taylor series
//
Vec2 TouchTracker::correctPeak(const MLSignal& in, int px, int py)
{

	Vec2 pos = fractionalPeakTaylor(in, px, py); 	
	 // debug() << "correct: [" << px << ", " << py << "] -> [" << pos.x() <<  ", " << pos.y() << "]\n";

	pos = vclamp(pos, minPos, maxPos);
	return pos;
}
*/

class compareZ 
{
public:
	bool operator() (const Vec3& a, const Vec3& b) 
	{ 
		return (a[2] > b[2]);
	}
};


/*
// find peaks by comparing successive samples in each row and each column,
// and crossing off pixels that are lower than others. The pixels remaining
// are the peaks of their 4-neighborhoods.
//
// new peaks are added to the peak list. 
// 
void TouchTracker::findPeaks(const MLSignal& in)
{
	if (!mpInputMap) return;
	int width = in.getWidth();
	int height = in.getHeight();
	unsigned char *	pMapRow;
	int i, j;
	bool slope, prevSlope;
	memset(mpInputMap, 0, width*height*sizeof(e_pixdata));
	
	// left / right
	for (j=0; j<height; ++j)
	{
		pMapRow = mpInputMap + j*mWidth;
		prevSlope = true;
		for(i=0; i<width - 1; ++i)
		{
			slope = in(i + 1, j) > in(i, j);
			if (!prevSlope || slope)
			{
				pMapRow[i] = 1;
			}
			prevSlope = slope;
		}
		i = width - 1; // last column
		{
			slope = false;
			if (!prevSlope || slope)
			{
				pMapRow[i] = 1;
			}
		}
	}
	
	// up / down
	for (i=0; i<width; ++i)
	{
		prevSlope = true;
		for(j=0; j<height - 1; ++j)
		{
			pMapRow = mpInputMap + j*mWidth;
			slope = in(i, j + 1) > in(i, j);
			if (!prevSlope || slope)
			{
				pMapRow[i] = 1;
			}
			prevSlope = slope;
		}
		j = height-1; // last row
		pMapRow = mpInputMap + j*mWidth;
		{
			slope = false;
			if (!prevSlope || slope)
			{
				pMapRow[i] = 1;
			}
		}
	}
	
	// gather all peaks above fixed height.
	mPeaks.clear();
	for (int j=0; j<mHeight; j++)
	{
		pMapRow = mpInputMap + j*mWidth;
		for (int i=0; i < mWidth; i++)
		{
			// if peak
			if (pMapRow[i] == 0)
			{
				float z = in(i, j);
				if (z > mOnThreshold)
				{
					mPeaks.push_back(Vec3(i, j, z));
					if (mPeaks.size() >= kTouchTrackerMaxPeaks) goto done;
				}
			}
		}
	}
	
done:
	std::sort(mPeaks.begin(), mPeaks.end(), compareZ());
	int p = mPeaks.size();
	if(p > 0)
	{
		debug() << p << " peaks\n";
	}
}
*/

void TouchTracker::setLopass(float k)
{ 
	mLopass = k; 
	/*
	for(int t = 0; t < mTouchesPerFrame; ++t)
	{	
		mTouchFilters[t].setZ(mTouches[t].z);
	}
	*/
}
			
// --------------------------------------------------------------------------------
#pragma mark main processing

class compareTouchZ 
{
public:
	bool operator() (const Touch& a, const Touch& b) 
	{ 
		return (a.z > b.z);
	}
};

// with whole input minus background:
// for all touches t from most pressure to least:
//   use taylor correct to get new position
//   if other touches are close, snap position towards nearest unoccupied key center
//   subtract synthetic kernel for t from R

// expire or move existing touches based on new signal input. 
// 
void TouchTracker::updateTouches(const MLSignal& in)
{
	// copy input signal to border land
	int width = in.getWidth();
	int height = in.getHeight();
	
	mTemp.copy(in);
	mTemplateMask.clear();
	
	// sort active touches by Z
	// copy into sorting container, referring back to unsorted touches
	int activeTouches = 0;
	for(int i = 0; i < mTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			t.unsortedIdx = i;
			mTouchesToSort[activeTouches++] = t;
		}
	}

	std::sort(mTouchesToSort.begin(), mTouchesToSort.begin() + activeTouches, compareTouchZ());

	// update active touches in sorted order, referring to existing touches in place
	for(int i = 0; i < activeTouches; ++i)
	{
		int refIdx = mTouchesToSort[i].unsortedIdx;
		Touch& t = mTouches[refIdx];
		Vec2 pos(t.x, t.y); 
		Vec2 newPos = pos;
		float newX = t.x;
		float newY = t.y;
		float newZ = in(pos);
		
		// if not preparing to remove, update position.
		if (t.releaseCtr == 0)
		{
			Vec2 minPos(0, 0);
			Vec2 maxPos(width, height);
			int ix = floor(pos.x() + 0.5f);
			int iy = floor(pos.y() + 0.5f);								
			
			// move to any higher neighboring integer value
			Vec2 newPeak;
			newPeak = adjustPeak(mTemp, ix, iy);			
			Vec2 newPeakI, newPeakF;
			newPeak.getIntAndFracParts(newPeakI, newPeakF);
			int newPx = newPeakI.x();
			int newPy = newPeakI.y();
			
			// get exact location and new key
			Vec2 correctPos = mTemp.correctPeak(newPx, newPy);
			newPos = correctPos;	
			int newKey = getKeyIndexAtPoint(newPos);												
			
			// move the touch.  
			if((newKey == t.key) || !keyIsOccupied(newKey))
			{			
				// This must be the only place a touch can move from key to key.
				pos = newPos;
				newX = pos.x();
				newY = pos.y();					
				t.key = newKey;
			}							
		}
		
		// look for reasons to release
		newZ = in(newPos);
		bool thresholdTest = (newZ > mOffThreshold);			
		float inhibit = getInhibitThreshold(pos);
		bool inhibitTest = (newZ > inhibit);
		t.tDist = mCalibrator.differenceFromTemplateTouchWithMask(mTemp, pos, mTemplateMask);
		bool templateTest = (t.tDist < mTemplateThresh);
		bool override = (newZ > mOverrideThresh);
						
		t.age++;

		// handle release		
		// TODO get releaseDetect: evidence that touch has been released.
		// from matching release curve over ~ 50 samples. 
		if (!thresholdTest || (!templateTest && !override) || (!inhibitTest))
		{			
			/* debug
			if(!thresholdTest && (t.releaseCtr == 0))
			{
				debug() << refIdx << " REL thresholdFail: " << newZ << " at " << pos << "\n";	
			}
			if(!inhibitTest && (t.releaseCtr == 0))
			{
				debug() << refIdx << " REL inhibitFail: " << newZ << " < " << inhibit << "\n";	
			}
			if(!templateTest && (t.releaseCtr == 0))
			{
				debug() << refIdx << " REL templateFail: " << t.tDist << " at " << pos << "\n";	
			}			
			*/
			if(t.releaseCtr == 0)
			{
				t.releaseSlope = t.z / (float)kTouchReleaseFrames;
			}
			t.releaseCtr++;
			newZ = t.z - t.releaseSlope;
		}
		else
		{	
			// reset off counter 
			t.releaseCtr = 0;	
		}

		// filter and assign new touch values
		const float e = 2.718281828;
		float xyCutoff = (newZ - mOnThreshold) / (mMaxForce*0.25);
		xyCutoff = clamp(xyCutoff, 0.f, 1.f);
		xyCutoff *= xyCutoff;
		xyCutoff = xyCutoff*100.;
		xyCutoff = clamp(xyCutoff, 1.f, 100.f);
		float x = powf(e, -kMLTwoPi * xyCutoff / (float)mSampleRate);
		float a0 = 1.f - x;
		float b1 = -x;
		t.dz = newZ - t.z;		
		t.x = a0*newX - b1*t.x;
		t.y = a0*newY - b1*t.y;
		t.z = newZ;
		
		// filter z based on user lowpass setting and touch age
		float lp = mLopass;
		lp -= t.age*(mLopass*0.75f/kAttackFrames);
		lp = clamp(lp, mLopass, mLopass*0.25f);		
		float xz = powf(e, -kMLTwoPi * lp / (float)mSampleRate);
		float a0z = 1.f - xz;
		float b1z = -xz;
		t.zf = a0z*(newZ - mOnThreshold) - b1z*t.zf;	

		// remove touch if filtered z is below threshold
		if(t.zf < 0.)
		{
			// debug() << refIdx << " OFF with z:" << t.z << " td:" << t.tDist <<  "\n";	
			removeTouchAtIndex(refIdx);
		}
		
		// subtract updated touch from the input sum.
		// mTemplateScaled is scratch space. 
		mTemplateScaled.clear();
		mTemplateScaled.add2D(mCalibrator.getTemplate(pos), 0, 0);
		mTemplateScaled.scale(-t.z*mCalibrator.getZAdjust(pos)); 
														
		mTemp.add2D(mTemplateScaled, Vec2(pos - Vec2(kTemplateRadius, kTemplateRadius)));
		mTemp.sigMax(0.0);

		// add touch neighborhood to template mask. This allows crowded touches
		// to pass the template test by ignoring areas shared with other touches.
		Vec2 maskPos(t.x, t.y);
		const MLSignal& tmplate = mCalibrator.getTemplate(maskPos);
		mTemplateMask.add2D(tmplate, maskPos - Vec2(kTemplateRadius, kTemplateRadius));
	}
}

Vec3 TouchTracker::closestTouch(Vec2 pos)
{
	float minDist = MAXFLOAT;
	int minIdx = 0;
	for(int i = 0; i < mTouchesPerFrame; ++i)
	{
		Touch& t = mTouches[i];
		if (t.isActive())
		{
			Vec2 p(t.x, t.y);
			p -= pos;
			float d = p.magnitude();
			if(d < minDist)
			{
				minDist = d;
				minIdx = i;
			}
		}
	}
	Touch& t = mTouches[minIdx];
	return(Vec3(t.x, t.y, t.z));
}

// return the greatest inhibit threshold of any touch at the given position.		
float TouchTracker::getInhibitThreshold(Vec2 a)
{
	float inhibitMax = 1.1f;
	float inhibitRange = 6.f;
	float maxInhibit = 0.f;
	for(int i = 0; i < mTouchesPerFrame; ++i)
	{
		const Touch& u = mTouches[i];
		if (u.isActive())
		{
			// don't check against touch at same exact position (self)
			Vec2 b(u.x, u.y);				
			float dab = (a - b).magnitude();			
			if(dab > 0.1f)
			{
				float dr = 1.f / (1.f + dab*(1.f/inhibitRange));							
				float inhibitZ = inhibitMax*u.z*dr;
				maxInhibit = max(maxInhibit, inhibitZ);
			}
		}
	}
	return maxInhibit;
}
	
void TouchTracker::addPeakToKeyState(const MLSignal& in)
{
	mTemp.copy(in);
	for(int i=0; i<kMaxPeaksPerFrame; ++i)
	{
		// get highest peak from temp.  
		Vec3 peak = mTemp.findPeak();	
		float z = peak.z();
		
		// add peak to key state, or bail
		if (z > mOnThreshold*0.25f)
		{	
		
			Vec2 pos = in.correctPeak(peak.x(), peak.y());	
			int key = getKeyIndexAtPoint(pos);
			if(within(key, 0, mNumKeys))
			{
				// send peak energy to key under peak.
				KeyState& keyState = mKeyStates[key];
				MLRange kdzRange(mOnThreshold, mMaxForce*0.5, 0.001f, 1.f);
				float iirCoeff = kdzRange.convertAndClip(z);	
				float dt = mCalibrator.differenceFromTemplateTouch(in, pos);	
				keyState.mK = iirCoeff;
				keyState.zIn = z;
				keyState.dtIn = dt;
				if(mQuantizeToKey)
				{
					keyState.posIn = keyState.mKeyCenter;
				}
				else
				{
					keyState.posIn = pos;
				}
			}
		}
		else
		{
			break;
		}
	}
}							

void TouchTracker::findTouches()
{
	for(int i=0; i<mNumKeys; ++i)
	{
		float z = mKeyStates[i].zOut;	
		bool threshTest = (z > mOnThreshold);
		if(threshTest)
		{			
			float kdt = mKeyStates[i].dtOut;
			float kdz = mKeyStates[i].dzOut * 50.f;
			kdz = clamp(kdz, 0.f, 1.f);
			kdz = sqrtf(kdz);
			float kCoeff = mKeyStates[i].mK;
			Vec2 pos = mKeyStates[i].posOut;
			float inhibit = getInhibitThreshold(pos);			
			bool inhibitTest = (z > inhibit);
			bool templateTest = (kdt < mTemplateThresh);
			bool override = (z > mOverrideThresh);
			bool kCoeffTest = (kCoeff > 0.001f);

			bool ageTest = mKeyStates[i].age > 10;
			
			if (ageTest && inhibitTest && kCoeffTest)
			{	
//debug() << "key:" << i << " z:" << z << " dz:" << kdz << " template:" << templateTest << " inhibit:" << inhibitTest << "\n";
			
				// if difference of peak neighborhood from template at subpixel position 
				// is under threshold, we may have a new touch.		
				if (templateTest || override) 
				{
					if(!keyIsOccupied(i))
					{
					//	Touch t(pos.x(), pos.y(), z, kCoeff);
						Touch t(pos.x(), pos.y(), z, kdz);
						t.key = i;
						t.tDist = kdt;
						int newIdx = addTouch(t); 	
						if(newIdx >= 0)
						{		
							mKeyStates[i].age = 0;										
							

/*
												
debug() << newIdx << " ON z:" << z << " kdt:" << kdt << " at " << pos << "\n";	
debug() << "          template: " << templateTest << " override: " << override << "\n";
			debug() << "********\n";
			debug() << "KC " << kCoeff << "\n";
			debug() << "KDZ " << kdz << "\n";
			debug() << "********\n";
*/
			
			// set age for key state to inhibit retrigger
			
						}
					}

				}
			}
		}
	}
}

void TouchTracker::KeyState::tick()
{	
	// filter
	dzIn = (zIn - zOut);
	zOut += dzIn*mK;
	dtOut += (dtIn - dtOut)*mK;
	posOut += (posIn - posOut)*mK;
	
	// collect dz with constant IIR filter
	dzOut += (dzIn - dzOut)*mK;
	
	age++;

	// set inputs back to defaults 
	zIn = 0.f;
	dzIn = 0.f;
	dtIn = 1.0f;
	posIn = mKeyCenter; 
}

void TouchTracker::process(int)
{	
	if (!mpIn) return;
	const MLSignal& in(*mpIn);
	
	if (mNeedsClear)
	{
		mBackground.copy(in);
		mBackgroundFilter.clear();
		mNeedsClear = false;
		return;
	}
		
	if (mCalibrator.isCalibrating())
	{		
		mFilteredInput.copy(in);

		// smooth input	
		float kc, ke, kk;
		kc = 4./16.; ke = 2./16.; kk=1./16.;
		mFilteredInput.convolve3x3r(kc, ke, kk);
		int done = mCalibrator.addSample(mFilteredInput);
		
		if(done == 1)
		{
			// Tell the listener we have a new calibration. We still do the calibration here in the Tracker, 
			// but the Model will be responsible for saving and restoring the calibration maps.
			if(mpListener)
			{
				mpListener->hasNewCalibration(mCalibrator.mCalibrateSignal, mCalibrator.mNormalizeMap, mCalibrator.mAvgDistance);
			}
		}
	}
	else
	{
		// normalize before smoothing!
		mFilteredInput.copy(in);
		mCalibrator.normalizeInput(mFilteredInput);

		// smooth input	
		float kc, ke, kk;
		kc = 4./16.; ke = 2./16.; kk=1./16.;
		mFilteredInput.convolve3x3r(kc, ke, kk);
	
		// build sum of currently tracked touches	
		//
		mSumOfTouches.clear();
		int numActiveTouches = 0;
		for(int i = 0; i < mTouchesPerFrame; ++i)
		{
			const Touch& t(mTouches[i]);
			if(t.isActive())
			{
				Vec2 touchPos(t.x, t.y);
				mTemplateScaled.clear();
				mTemplateScaled.add2D(mCalibrator.getTemplate(touchPos), 0, 0);
				mTemplateScaled.scale(t.z*mCalibrator.getZAdjust(touchPos));
				mSumOfTouches.add2D(mTemplateScaled, touchPos - Vec2(kTemplateRadius, kTemplateRadius));
				numActiveTouches++;
			}
		}	
		
		// to make sum of touches a bit bigger 
		//mSumOfTouches.scale(1.5f);
		//mSumOfTouches.convolve3x3r(kc, ke, kk);

		//
		// TODO lots of optimization here in onepole, 2D filter
		//
		// TODO the mean of lowpass background can be its own control source that will 
		// act like an accelerometer!  tilt controls even. 
		
		mBackgroundFilterFrequency.fill(mBackgroundFilterFreq);
		
		// build background: lowpass filter rest state.  Filter freq.
		// is nonzero where there are no touches, 0 where there are touches.
		mTemp.copy(mSumOfTouches);
		mTemp.scale(100.f); 
		mBackgroundFilterFrequency.subtract(mTemp);
		mBackgroundFilterFrequency.sigMax(0.);		
		
		// TODO allow filter to move a little if touch template distance is near threshold
		// this will fix most stuck touches

		// filter background in up direction 
		mBackgroundFilterFrequency2.fill(mBackgroundFilterFreq);
		mBackgroundFilter.setInputSignal(&mFilteredInput);
		mBackgroundFilter.setOutputSignal(&mBackground);

		// set asymmetric filter coeffs and get background
		mBackgroundFilter.setCoeffs(mBackgroundFilterFrequency, mBackgroundFilterFrequency2);
		mBackgroundFilter.process(1);	

		// subtract background from input 
		//
		mInputMinusBackground.copy(mFilteredInput);
		mInputMinusBackground.subtract(mBackground);
 						
		// move or remove and filter existing touches
		//
		updateTouches(mInputMinusBackground);	
		
		// TODO can negative values be used to inhibit nearby touches?  
		// this might prevent sticking touches when a lot of force is
		// applied then quickly released.
		
		// TODO look for retriggers here, touches not fallen to 0 but where 
		// dz warrants a new note-on. The way to do this is keep a second,
		// separate set of key states that do not get cleared by current
		// touches.  These can be used to get the dz values and using the exact
		// same math, velocities will match other note-ons.

		// after update Touches, subtract sum of touches to get residual R
		// R = input - T.
		// This represents any pressure data not currently part of a touch.
		mInputMinusBackground.sigMax(0.);	
		mResidual.copy(mInputMinusBackground);
		mResidual.subtract(mSumOfTouches);
		mResidual.sigMax(0.);

		// get signals for viewer
		// TODO optimize: we only have to copy these each time a view is needed
		mCalibratedSignal.copy(mInputMinusBackground);
		mCookedSignal.copy(mSumOfTouches);		
		mTestSignal.copy(mResidual);		
		
		// get subpixel xyz peak from residual
		addPeakToKeyState(mResidual);
		
		// update key states 
		for(int i=0; i<mNumKeys; ++i)
		{
			mKeyStates[i].tick();
		}
		
		findTouches();
		
		// write touch data to one frame of output signal.
		//
		MLSignal& out = *mpOut;
		for(int t = 0; t < mTouchesPerFrame; ++t)
		{
			int age = mTouches[t].age;
			out(xColumn, t) = mTouches[t].x;
			out(yColumn, t) = mTouches[t].y;
			out(zColumn, t) = (age > 0) ? mTouches[t].zf : 0.;			
			out(dzColumn, t) = mTouches[t].dz;
			out(ageColumn, t) = age;
			out(dtColumn, t) = mTouches[t].tDist;
			// NOTE: the note column is filled in later by the Model.
		}
	}
	
#if DEBUG	
	// TEMP	
	if (mCount++ > 1000) 
	{
		mCount = 0;
		dumpTouches();
	}
#endif	
}

void TouchTracker::setDefaultCalibration()
{
	mCalibrator.setDefaultCalibration();
	mpListener->hasNewCalibration(mNullSig, mNullSig, -1.f);
}

// --------------------------------------------------------------------------------
#pragma mark calibration


TouchTracker::Calibrator::Calibrator(int w, int h) :
	mActive(false),
	mHasCalibration(false),
	mHasNormalizeMap(false),
	mSrcWidth(w),
	mSrcHeight(h),
	mWidth(kCalibrateWidth),
	mHeight(kCalibrateHeight),
	mAutoThresh(0.05f)
{
	// resize sums vector and signals in it
	mData.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mDataSum.resize(mWidth*mHeight);//*kCalibrateResolution*kCalibrateResolution);
	mSampleCount.resize(mWidth*mHeight);
	mPassesCount.resize(mWidth*mHeight);
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		mData[i].setDims(kTemplateSize, kTemplateSize);
		mData[i].clear();
		mDataSum[i].setDims(kTemplateSize, kTemplateSize);
		mDataSum[i].clear();
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mIncomingSample.setDims(kTemplateSize, kTemplateSize);
	mVisSignal.setDims(mWidth, mHeight);
	
	mNormalizeMap.setDims(mSrcWidth, mSrcHeight);
	mNormalizeCount.setDims(mSrcWidth, mSrcHeight);
	mFilteredInput.setDims(mSrcWidth, mSrcHeight);

	makeDefaultTemplate();
}

TouchTracker::Calibrator::~Calibrator()
{
}

void TouchTracker::Calibrator::begin()
{
	debug() << "\n****************************************************************\n\n";
	debug() << "Hello and welcome to calibration. \n";
	debug() << "Collecting threshold, please don't touch";
	
	mFilteredInput.clear();
	mSampleCount.clear();
	mPassesCount.clear();
	mVisSignal.clear();
	mNormalizeMap.fill(1.f);
	mNormalizeCount.fill(0.f);
	mTotalSamples = 0;
	mStartupSum = 0.;
	for(int i=0; i<mWidth*mHeight; ++i)
	{
		mData[i].fill(MAXFLOAT);  // for minimum gathering
		mDataSum[i].clear(); 
		mSampleCount[i] = 0;
		mPassesCount[i] = 0;
	}
	mPeak = Vec2();
	age = 0;
	mActive = true;
	mHasCalibration = false;
}

void TouchTracker::Calibrator::cancel()
{
	if(isCalibrating())
	{
		mActive = false;
		debug() << "\nCalibration cancelled.\n";
	}
}

void TouchTracker::Calibrator::setDefaultCalibration()
{
	mActive = false;
	mHasCalibration = false;
	mHasNormalizeMap = false;
}

void TouchTracker::Calibrator::makeDefaultTemplate()
{
	mDefaultTemplate.setDims (kTemplateSize, kTemplateSize);
		
	int w = mDefaultTemplate.getWidth();
	int h = mDefaultTemplate.getHeight();
	Vec2 vcenter(h/2.f, w/2.f);
	
	// default scale -- not important because we want to calibrate.
	Vec2 vscale(3.5, 3.);	 
	
	for(int j=0; j<h; ++j)
	{
		for(int i=0; i<w; ++i)
		{
			Vec2 vdistance = Vec2(i + 0.5f, j + 0.5f) - vcenter;
			vdistance /= vscale;
			float d = clamp(vdistance.magnitude(), 0.f, 1.f);
			mDefaultTemplate(i, j) = 1.0*(1.0f - d);
		}
	}
}

const MLSignal& TouchTracker::Calibrator::getTemplate(Vec2 p) const
{
	static MLSignal temp1(kTemplateSize, kTemplateSize);
	static MLSignal temp2(kTemplateSize, kTemplateSize);
	if(mHasCalibration)
	{
		Vec2 pos = getBinPosition(p);
		Vec2 iPos, fPos;
		pos.getIntAndFracParts(iPos, fPos);	
		int idx00 = iPos.y()*mWidth + iPos.x();
		int idx10 = idx00;
		int idx01 = idx00;
		int idx11 = idx00;
		if(iPos.x() < mWidth - 1)
		{
			idx10 += 1;
			idx11 += 1;
		}
		if(iPos.y() < mHeight - 1)
		{
			idx01 += mWidth;
			idx11 += mWidth;
		}
		const MLSignal d00 = mCalibrateSignal.getFrame(idx00);
		const MLSignal d10 = mCalibrateSignal.getFrame(idx10);
		const MLSignal d01 = mCalibrateSignal.getFrame(idx01);
		const MLSignal d11 = mCalibrateSignal.getFrame(idx11);		
		temp1.copy(d00);
		temp1.sigLerp(d10, fPos.x());
		temp2.copy(d01);
		temp2.sigLerp(d11, fPos.x());
		temp1.sigLerp(temp2, fPos.y());
		return temp1;
	}
	else
	{
		return mDefaultTemplate;
	}
}

Vec2 TouchTracker::Calibrator::getBinPosition(Vec2 pIn) const
{
	// Soundplane A
	Vec2 pOut = vclamp(pIn, Vec2(2.5, 0.5), Vec2(60.49, 6.49));
	pOut -= Vec2(2.5, 0.5);
	Vec2 pOutI, pOutF;
	pOut.getIntAndFracParts(pOutI, pOutF);
	
	
	/*
	static MLRange binRangeX(3.5, 59.5, 1., 29.);
	static MLRange binRangeY(1.25, 5.75, 1., 4.);
	Vec2 minPos(0., 0.);
	Vec2 maxPos(mWidth - 1.00f, mHeight - 1.00f);
	Vec2 pos(binRangeX(p.x()), binRangeY(p.y()));
	return vclamp(pos, minPos, maxPos);
	*/
	
//debug() << "bin: " << pOutI << "\n";
	
	return pOut; // TEST
}

void TouchTracker::Calibrator::normalizeInput(MLSignal& in)
{
	if(mHasNormalizeMap)
	{
		in.multiply(mNormalizeMap);
	}
}

void TouchTracker::Calibrator::makeNormalizeMap()
{	
	
	// blur 
//	float kc, ke, kk;
//	kc = 4./16.; ke = 2./16.; kk=1./16.;
//	mNormalizeMap.convolve3x3r(kc, ke, kk);
//	mNormalizeCount.convolve3x3r(kc, ke, kk);

	// get mean of nonzero data
	int samples = 0;
	float sum = 0.;
	for(int j=0; j<mSrcHeight; ++j)
	{
		for(int i=0; i<mSrcWidth; ++i)
		{			
			float c = mNormalizeCount(i, j);
			if(c > 0.)
			{
				sum += mNormalizeMap(i, j) / c;
				samples++;
			}
		}
	}
	float m = sum / (float)samples;
	
	// fill in zeroes and get scale.
	for(int j=0; j<mSrcHeight; ++j)
	{
		for(int i=0; i<mSrcWidth; ++i)
		{			
			float c = mNormalizeCount(i, j);
			if(c <= 0.)
			{
				mNormalizeCount(i, j) = 1.;
				mNormalizeMap(i, j) = m;
			}						
			mNormalizeMap(i, j) = m / (mNormalizeMap(i, j) / mNormalizeCount(i, j));
		}
	}
	
	// correct data at top/bottom edges (ad-hoc)
	for(int i=0; i<mSrcWidth; ++i)
	{
		mNormalizeMap(i, 0) *= 1.7;
		mNormalizeMap(i, 1) *= 1.45;
		mNormalizeMap(i, mSrcHeight - 2) *= 1.1;
		mNormalizeMap(i, mSrcHeight - 1) *= 1.33;
	}
	
	
	debug() << "************ MAP: \n";
	mNormalizeMap.dump(mNormalizeMap.getBoundsRect());
	
	mHasNormalizeMap = true;
}

void TouchTracker::Calibrator::getAverageTemplateDistance()
{
	MLSignal temp(mWidth, mHeight);
	MLSignal tempSample(kTemplateSize, kTemplateSize);
	float sum = 0.f;
	int samples = 0;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{						
			int idx = j*mWidth + i;
			
			// put mean of input samples into temp signal at i, j
			temp.clear();
			tempSample.copy(mDataSum[idx]);
			tempSample.scale(1.f / (float)mSampleCount[idx]);
			temp.add2D(tempSample, i - kTemplateRadius, j - kTemplateRadius);
			
			float diff = differenceFromTemplateTouch(temp, Vec2(i, j));
			sum += diff;
			samples++;
			// debug() << "template diff [" << i << ", " << j << "] : " << diff << "\n";	
		}
	}
	mAvgDistance = sum / (float)samples;
}

int TouchTracker::Calibrator::addSample(const MLSignal& m)
{
	int r = 0;
	static Vec2 intPeak1;
	static MLSignal f2(mSrcWidth, mSrcHeight);
	
	// simple lopass filter for calibration
	f2 = m;
	f2.subtract(mFilteredInput);
	f2.scale(0.1f);
	mFilteredInput.add(f2);
	 
	// get peak of sample data
	Vec3 inPeak = m.findPeak();
	
	int ipx = (int)inPeak.x();
	int ipy = (int)inPeak.y();
	ipx = clamp(ipx, 2, 61);
	ipy = clamp(ipy, 0, 7);
		
	// get float correct peak
//	Vec2 fPeak = mFilteredInput.correctPeak(inPeak.x(), inPeak.y());
	Vec2 fPeak(inPeak.x(), inPeak.y());
	
	// lowpass
	if(age == 0)
	{
		mPeak = fPeak;
	}
	else
	{
		mPeak += (fPeak - mPeak) * 0.1f;
	}
	
//	float peakC = mFilteredInput(mPeak);
	float peakC = mFilteredInput((int)mPeak.x(), (int)mPeak.y());

	// get integer bin
	Vec2 binPeak = getBinPosition(mPeak);
	int bix = binPeak.x();
	int biy = binPeak.y();
	Vec2 bIntPeak(bix, biy);
	
	const int startupSamples = 1000;
	if (mTotalSamples < startupSamples)
	{
		age = 0;
		mStartupSum += peakC;
		if(mTotalSamples % 100 == 0)
		{
			debug() << ".";
		}
	}
	else if (mTotalSamples == startupSamples)
	{
		age = 0;
		// mAutoThresh = mStartupSum / (float)startupSamples * 10.f;	
		
		mAutoThresh = kCalibrateTrackerThresh;
		
		debug() << "\n\nRight. Now please slide a single finger over the  \n";
		debug() << "Soundplane surface with a light and even touch, \n";
		debug() << "visiting each key twice until all the keys are \n";
		debug() << "colored green at left.  Sliding over a key the \n";
		debug() << "first time will turn it gray.  Sliding over a \n";
		debug() << "key the second time will turn it green.\n";
		debug() << "\n";
		debug() << "(auto threshold: " << mAutoThresh << ") \n";
	}		
	else 
	{
		if(peakC > mAutoThresh)
		{
			age++; // continue touch
			// get sample from input around peak and normalize
			mIncomingSample.clear();
			mIncomingSample.add2D(m, -fPeak + Vec2(kTemplateRadius, kTemplateRadius)); 
			mIncomingSample.sigMax(0.f);
			
		float kc, ke, kk;
		kc = 4./16.; ke = 2./16.; kk=1./16.;
		mIncomingSample.convolve3x3r(kc, ke, kk);
			
			mIncomingSample.scale(1.f / mIncomingSample(kTemplateRadius, kTemplateRadius));

			// get sum and minimum of all samples
			int dataIdx = biy*mWidth + bix;
			mDataSum[dataIdx].add(mIncomingSample); 
			mData[dataIdx].sigMin(mIncomingSample); 
			mSampleCount[dataIdx]++;
			
			// store peak data for normalize 
			mNormalizeMap(ipx, ipy) += peakC;
			mNormalizeCount(ipx, ipy) += 1.0;		
						
			if(bIntPeak != intPeak1)
			{
				// entering new bin.
				intPeak1 = bIntPeak;
				mVisPeak = bIntPeak;		
				if(mPassesCount[dataIdx] < kPassesToCalibrate)
				{
					mPassesCount[dataIdx]++;
					mVisSignal(bix, biy) = (float)mPassesCount[dataIdx] / (float)kPassesToCalibrate;
				}
			}			
			
			// check for done
			if(isDone())
			{
				mCalibrateSignal.setDims(kTemplateSize, kTemplateSize, mWidth*mHeight);
				
				// get result for each junction
				for(int j=0; j<mHeight; ++j)
				{
					for(int i=0; i<mWidth; ++i)
					{						
						int idx = j*mWidth + i;
						// copy to 3d signal
						mCalibrateSignal.setFrame(idx, mData[idx]);
					}
				}
				
				getAverageTemplateDistance();
				makeNormalizeMap();				
				mHasCalibration = true;
				mActive = false;	
				r = 1;	
							
				debug() << "\n****************************************************************\n\n";
				debug() << "Calibration is now complete and will be auto-saved in the file \n";
				debug() << "SoundplaneAppState.txt. \n";
				debug() << "\n****************************************************************\n\n";
			}	
		}
		else
		{
			age = 0;
			intPeak1 = Vec2(-1, -1);
			mVisPeak = Vec2(-1, -1);
		}
	}
	mTotalSamples++;
	return r;
}

bool TouchTracker::Calibrator::isCalibrating()
{
	return mActive;
}

bool TouchTracker::Calibrator::hasCalibration()
{
	return mHasCalibration;
}

bool TouchTracker::Calibrator::isDone()
{
	// If we have enough samples at each location, we are done.
	bool calDone = true;
	for(int j=0; j<mHeight; ++j)
	{
		for(int i=0; i<mWidth; ++i)
		{
			int dataIdx = j*mWidth + i;
			if (mPassesCount[dataIdx] < kPassesToCalibrate)
			{
				calDone = false;
				goto done;
			}
		}
	}
done:	
	return calDone;
}
	
void TouchTracker::Calibrator::setCalibration(const MLSignal& v)
{
	if((v.getHeight() == mSrcHeight) && v.getWidth() == mSrcWidth)
	{
		mCalibrateSignal = v;
		mHasCalibration = true;
	}
	else
	{
		debug() << "TouchTracker::Calibrator::setCalibration: restoring default.\n";
		mCalibrateSignal = v;
		mHasCalibration = false;
	}
}

void TouchTracker::Calibrator::setNormalizeMap(const MLSignal& v)
{
	if((v.getHeight() == mSrcHeight) && v.getWidth() == mSrcWidth)
	{
		mNormalizeMap = v;
		mHasNormalizeMap = true;
	}
	else
	{
		debug() << "TouchTracker::Calibrator::setNormalizeMap: restoring default.\n";
		mNormalizeMap.fill(1.f);
		mHasNormalizeMap = false;
	}
}


float TouchTracker::Calibrator::getZAdjust(const Vec2 p)
{
	// first adjust z for interpolation based on xy position within unit square
	//
	Vec2 vInt, vFrac;
	p.getIntAndFracParts(vInt, vFrac);
	
	Vec2 vd = (vFrac - Vec2(0.5f, 0.5f));
	return 1.414f - vd.magnitude()*0.5f;
}

float TouchTracker::Calibrator::differenceFromTemplateTouch(const MLSignal& in, Vec2 pos)
{
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 1.0;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos))
			{
				float inVal = in(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
} 

float TouchTracker::Calibrator::differenceFromTemplateTouchWithMask(const MLSignal& in, Vec2 pos, const MLSignal& mask)
{
	static float maskThresh = 0.001f;
	static MLSignal a2(kTemplateSize, kTemplateSize);
	static MLSignal b(kTemplateSize, kTemplateSize);
	static MLSignal b2(kTemplateSize, kTemplateSize);

	float r = 0.f;
	int height = in.getHeight();
	int width = in.getWidth();
	MLRect boundsRect(0, 0, width, height);
	
	// use linear interpolated z value from input
	float linearZ = in(pos)*getZAdjust(pos);
	linearZ = clamp(linearZ, 0.00001f, 1.f);
	float z1 = 1./linearZ;	
	const MLSignal& a = getTemplate(pos);
	
	// get normalized input values surrounding touch
	int tr = kTemplateRadius;
	b.clear();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			Vec2 vInPos = pos + Vec2((float)i - tr,(float)j - tr);			
			if (boundsRect.contains(vInPos) && (mask(vInPos) < maskThresh))
			{
				float inVal = in(vInPos);
				inVal *= z1;
				b(i, j) = inVal;
			}
		}
	}
	
	int tests = 0;
	float sum = 0.;

	// add differences in z from template
	a2.copy(a);
	b2.copy(b);
	a2.dx();
	b2.dx();
	for(int j=0; j < kTemplateSize; ++j)
	{
		for(int i=0; i < kTemplateSize; ++i)
		{
			if(b(i, j) > 0.)
			{
				float d = a2(i, j) - b2(i, j);
				sum += d*d;
				tests++;
			}
		}
	}

	// get RMS difference
	if(tests > 0)
	{
		r = sqrtf(sum / tests);
	}	
	return r;
}



// --------------------------------------------------------------------------------
#pragma mark utilities

void TouchTracker::dumpTouches()
{
	int c = 0;
	for(int i = 0; i < mTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if(t.isActive())
		{
			debug() << "t" << i << ":" << t << ", key#" << t.key << ", td " << t.tDist << ", dz " << t.dz << "\n";		
			c++;
		}
	}
	if(c > 0)
	{
		debug() << "\n";
	}
}

int TouchTracker::countActiveTouches()
{
	int c = 0;
	for(int i = 0; i < mTouchesPerFrame; ++i)
	{
		const Touch& t = mTouches[i];
		if( t.isActive())
		{
			c++;
		}
	}
	return c;
}

