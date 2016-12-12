#include "cinder/app/App.h"
#include "cinder/app/RendererGl.h"
#include "cinder/app/Platform.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/Xml.h"
#include "cinder/Timeline.h"
#include "cinder/ImageIo.h"
#include "cinder/Thread.h"
#include "cinder/Rand.h"
#include "cinder/Perlin.h"
#include "cinder/ConcurrentCircularBuffer.h"
#include "cinder/gl/TextureFont.h"
#include "cinder/qtime/QuickTimeGl.h"
#include "cinder/Json.h"
#include "Resources.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <time.h>
#include <string>
#include <cstdio> // for std::remove

//TODO: ADD SCREEN WITH TIMEEDIT SCHEDULE

#include <boost/config.hpp>
#ifdef BOOST_NO_STDC_NAMESPACE
namespace std { using ::remove; }
#endif

#include "yaml.h"

using namespace ci;
using namespace ci::app;
using namespace std;

#pragma mark Globals

static const bool PREMULT = false;

bool gTriggerTransition;

void triggerTransition(){
    gTriggerTransition = true;
}

std::string expand_user(std::string path) {
    if (not path.empty() and path[0] == '~') {
        assert(path.size() == 1 or path[1] == '/');  // or other error handling
        char const* home = getenv("HOME");
        if (home or ((home = getenv("USERPROFILE")))) {
            path.replace(0, 1, home);
        }
        else {
            char const *hdrive = getenv("HOMEDRIVE"),
            *hpath = getenv("HOMEPATH");
            assert(hdrive);  // or other error handling
            assert(hpath);
            path.replace(0, 1, std::string(hdrive) + hpath);
        }
    }
    return path;
}

#pragma mark FadingTexture

class FadingTexture {
public:
    FadingTexture();
    void draw();
    void fadeToSurface(SurfaceRef newSurface, float duration=1.5f);
    void fadeToSurface(float duration=1.5f);
    
    Anim<float>     mCrossFade;
    Anim<float>     mFade;
    Area            mBounds;
    Color           mColor;
    gl::TextureRef  mTexture, mLastTexture;
    
};

class Snowflake {
public:
    vec2 position;
    vec2 speed;
    float radius;
};

// Functor which empties the FadingTexture pointed to by ftPtr
struct ResetFadingTextureFunctor {
	ResetFadingTextureFunctor( FadingTexture *ftPtr )
    : mFadingTexturePtr( ftPtr )
	{}
	
	void operator()() {
		mFadingTexturePtr->mLastTexture.reset();
		mFadingTexturePtr->mTexture.reset();
	}
	
	FadingTexture	*mFadingTexturePtr;
};

FadingTexture::FadingTexture(){
    mFade = 0.0;
    mCrossFade = 0.0;
    mColor = Color::white();
}

void FadingTexture::draw(){
    
    Rectf drawBounds;
    
	if( mLastTexture ) {
		gl::color( mColor.r, mColor.g, mColor.b, (1.0f - mCrossFade)*mFade );
		Rectf textureBounds = mLastTexture->getBounds();
		drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mLastTexture, drawBounds );
    }
	if( mTexture ) {
		gl::color( mColor.r, mColor.g, mColor.b, mCrossFade*mFade );
		Rectf textureBounds = mTexture->getBounds();
		drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mTexture, drawBounds );
	}
    {
    gl::ScopedBlendAdditive  blendAdditive;
//    gl::enableAdditiveBlending();
    gl::color( 1.0-mColor.r, 1.0-mColor.g, 1.0-mColor.b, mFade*.5 );
    gl::drawSolidRect(drawBounds);
    }
//    gl::enableAlphaBlending();
    
}

void FadingTexture::fadeToSurface(float duration){
    timeline().apply( &mFade, 0.0f, duration ).finishFn(ResetFadingTextureFunctor( this ));
}

void FadingTexture::fadeToSurface(SurfaceRef newSurface, float duration){
    
    if( newSurface ) {
        mLastTexture = mTexture; // the "last" texture is now the current text
        
        mTexture = gl::Texture::create( *newSurface.get() );
        
        // blend from 0 to 1 over 1.5sec
        timeline().apply( &mCrossFade, 0.0f, 1.0f, duration );
        timeline().apply( &mFade, 1.0f, duration );
        
    } else {
        timeline().apply( &mFade, 0.0f, duration );
    }
    
}

#pragma mark Project

class Project {
public:
    
    Project(const fs::path &p);
    void loadYAMLFile(const fs::path &pYAML);
    void setupResources(const fs::path &p);
    void reload();
    
    fs::path            mPath;
    vector<fs::path>    mResources;
    vector<fs::path>    mImages;
    vector<fs::path>    mMovies;
    
    //    ConcurrentCircularBuffer<Surface> *mSurfaces;
    
    boost::gregorian::date mDate;
    std::string         mTitle;
    std::string         mAbstract;
    std::string         mSummary;
    vector<std::string> mParticipants;
    vector<std::string> mCredits;
    vector<std::string> mMaterials;
    vector<std::string> mTags;
    std::string         mCreativeCommons;
    Url                 mHomepageURL;
    
};

Project::Project(const fs::path &p){
    
    //   mSurfaces = new ConcurrentCircularBuffer<Surface>( 5 ); // room for 5 images
    
    if(fs::exists(p) && fs::is_directory(p)){
        mPath = p;
        reload();
    }
}

void Project::reload(){
    if(fs::exists(mPath) && mPath != ""){
        mResources.clear();
        mImages.clear();
        mMovies.clear();
        setupResources(mPath);
        fs::path pYAML = fs::path(mPath.string() + "/project.yaml");
        if(fs::exists(pYAML)){
            loadYAMLFile(pYAML);
        }
    }
}

void Project::loadYAMLFile(const fs::path &pYAML){
    
    // load YAML file
    YAML::Node projectYaml = YAML::LoadFile(pYAML.c_str());
    
    if(projectYaml["date"]){
        try {
            mDate = boost::gregorian::from_string(projectYaml["date"].as<std::string>());
        } catch (...) {
            mDate = boost::gregorian::date(boost::date_time::not_a_date_time);
        }
    }
    
    if(projectYaml["title"]){
        mTitle = projectYaml["title"].as<std::string>();
    }
    
    if(projectYaml["abstract"]){
        mAbstract = projectYaml["abstract"].as<std::string>();
    }
    
    if(projectYaml["summary"]){
        mSummary = projectYaml["summary"].as<std::string>();
    }
    
    if(projectYaml["participants"]){
        mParticipants.clear();
        YAML::Node n = projectYaml["participants"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mParticipants.push_back((*it)["name"].as<std::string>());
    }
    
    if(projectYaml["credits"]){
        mCredits.clear();
        YAML::Node n = projectYaml["credits"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mCredits.push_back((*it)["name"].as<std::string>());
    }
    
    if(projectYaml["materials"]){
        mMaterials.clear();
        YAML::Node n = projectYaml["materials"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mMaterials.push_back(it->as<std::string>());
    }
    
    if(projectYaml["tags"]){
        mTags.clear();
        YAML::Node n = projectYaml["tags"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mTags.push_back(it->as<std::string>());
    }
    
    if(projectYaml["homepage"]){
        try {
            mHomepageURL = Url(projectYaml["homepage"].as<std::string>());
        } catch (...){
            mHomepageURL = Url();
        }
    }
    
    //std::string         mCreativeCommons;
    
    
}

void Project::setupResources(const fs::path &p){
    
    if(fs::exists(p) && fs::is_directory(p)){
        
        typedef vector<fs::path> paths;         // store paths,
        paths resPaths;                         // so we can sort them later
        
        copy(fs::directory_iterator(p), fs::directory_iterator(), back_inserter(resPaths));
        
        sort(resPaths.begin(), resPaths.end()); // sort, since directory iteration
        // is not ordered on some file systems
        
        reverse(resPaths.begin(), resPaths.end());
        
        for (paths::const_iterator resIt (resPaths.begin()); resIt != resPaths.end(); ++resIt)
        {
            
            if(boost::iequals(resIt->extension().string(), ".pdf") ){
                // pdf files
            }
            
            if(boost::iequals(resIt->extension().string(), ".mov") ||
               boost::iequals(resIt->extension().string(), ".mp4") ||
               boost::iequals(resIt->extension().string(), ".m4v") ||
               boost::iequals(resIt->extension().string(), ".avi") ){
                // movie files
                mResources.push_back(*resIt);
                mMovies.push_back(*resIt);
            }
            
            if(boost::iequals(resIt->extension().string(), ".jpg") ||
               boost::iequals(resIt->extension().string(), ".png") ||
               boost::iequals(resIt->extension().string(), ".gif") ||
               boost::iequals(resIt->extension().string(), ".jpeg") ){
                //image files
                mResources.push_back(*resIt);
                mImages.push_back(*resIt);
            }
        }
    }
    
}


#pragma mark AtriumDisplayApp

class AtriumDisplayApp : public App {
public:
	void setup();
	void mouseDown( MouseEvent event );
	void update();
	void draw();
    void loadNextProject();
	void shutdown();
    
    bool readConfig();
    
    void loadImagesThreadFn();
    
    ConcurrentCircularBuffer<SurfaceRef>	*mSurfaces;
    
    deque<Project*>        mProjects;
    
	void loadMovieFile( const fs::path &path );
    
    qtime::MovieGlRef		mMovie;
    gl::TextureRef			mMovieFrameTexture, mMovieInfoTexture;
    JsonTree                mMovieSubtitles;
	
    Project                 *mCurrentProject;
    
    gl::TextureRef	mTitleTexture, mHeaderTexture, mProjectTexture, mLogoTexture;
    
    gl::TextureFontRef      mTitleFontPrimary;
    gl::TextureFontRef      mTitleFontSecondary;
    
    Font                    mHeaderFont;
    Font                    mParagraphFont;
    Font                    mSubtitleFont;
    Font                    mSmallFont;
    Font                    mTagFont;
    
    Color                   mTintColor;
    
	bool					mShouldQuit;
	shared_ptr<thread>		mThread;
	FadingTexture			mFullTexture, mLeftTexture, mMidTexture, mRightTexture;
    FadingTexture *         mFadedTexture;
    int                     mFadedTextureFadeCount;
	Anim<float>				mFade;
    Anim<float>             mTitleFade;
    Anim<float>             mStripesNoise;
    Anim<vec2>              mStripesPosition;
    Anim<float>             mStripesFade;
    Anim<float>             mStripesSquareness;
    int                     mTransitionState;
    int                     mTransitionStateNext;
	double					mLastTime;
    Anim<float>             mLogoFade;
    Anim<float>             mHeaderFade;
    Anim<float>             mProjectTitleFade;
    Anim<float>             mProjectDetailsFade;
    Anim<float>             mMovieFade;
    int                     mTaglineStringPos;
    vector<string>          mTaglineStrings;
    
    vector<Snowflake>       mSnowflakes;
    
    YAML::Node              configYaml;
    
    fs::path                configResourcePath;
    
    Perlin                  perlin;
};

void AtriumDisplayApp::setup()
{
    hideCursor();
    
    srandomdev();
    randSeed(random());
    perlin.setSeed(randInt());
    
	mShouldQuit = false;
	mSurfaces = new ConcurrentCircularBuffer<SurfaceRef>( 3 ); // room for 5 images
    
    // create and launch the thread
    // mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
    
    mFullTexture.mBounds = getWindowBounds();
    mLeftTexture.mBounds.set(0, 0, getWindowWidth()/3.f, getWindowHeight());
    mMidTexture.mBounds.set(getWindowWidth()/3.f, 0, getWindowWidth()*2.f/3.f, getWindowHeight());
    mRightTexture.mBounds.set(getWindowWidth()*2.f/3.f, 0, getWindowWidth(), getWindowHeight());
    
    mTintColor = mFullTexture.mColor = mLeftTexture.mColor = mMidTexture.mColor = mRightTexture.mColor = Color(1.f,.85f, .75f);
    
    mTaglineStrings.push_back("full scale prototyping of computational spaces.");
    
    mTaglineStringPos = 0;
	
#pragma mark AtriumDisplayApp @Font loading
    
    gl::TextureFont::Format f;
    f.enableMipmapping( true );
    
    { // render itu logo
        TextLayout layout;
        layout.clear( ColorA( 0.1f, 0.1f, 0.1f, 1.f ) );
        layout.setFont( Font( loadResource(RES_CUSTOM_FONT_LIGHT), getWindowHeight()*0.09895 ) );
        layout.setColor( ColorA( 1., 1., 1., 1. ) );
        layout.addLine("  IT UNIVERSITY OF COPENHAGEN  ");
        Surface8u rendered = layout.render( true, PREMULT );
        mLogoTexture = gl::Texture::create( rendered );
    }
    { // texture fonts
        Font titleFontPrimary = Font( loadResource(RES_CUSTOM_FONT_BOLD), getWindowHeight()*0.4 );
        mTitleFontPrimary = gl::TextureFont::create( titleFontPrimary, f );
        Font titleFontSecondary = Font( loadResource(RES_CUSTOM_FONT_LIGHT), getWindowHeight()*0.4 );
        mTitleFontSecondary = gl::TextureFont::create( titleFontSecondary, f );
    }
    // Font objects
    mHeaderFont = Font( loadResource(RES_CUSTOM_FONT_LIGHT), getWindowHeight()*0.1 );
    mParagraphFont = Font( loadResource(RES_CUSTOM_FONT_REGULAR), getWindowHeight()*0.0475 );
    mSubtitleFont = Font( loadResource(RES_CUSTOM_FONT_SEMIBOLD), getWindowHeight()*0.065 );
    mSmallFont = Font( loadResource(RES_CUSTOM_FONT_SEMIBOLD), getWindowHeight()*0.025 );
    // mTagFont = Font( loadResource(RES_CUSTOM_FONT_REGULAR), getWindowHeight()*0.03 );
    mTagFont = Font( loadResource(RES_CUSTOM_FONT_REGULAR), getWindowHeight()*0.025 );
    mLastTime = getElapsedSeconds();
    
    //init vars
    
    mTransitionState = 0; 
    mTransitionStateNext = 0; 
    mFadedTextureFadeCount = 0;
    mTitleFade = 0;
    mHeaderFade = 0;
    mLogoFade = 0;
    mProjectTitleFade = 0;
    mProjectDetailsFade = 0;
    mMovieFade = 0;
    mStripesFade = 0;
    mStripesSquareness = 1;
    mStripesPosition = vec2(0,0);
    
    readConfig();
    
    triggerTransition();
    
}

void AtriumDisplayApp::loadImagesThreadFn()
{
	ci::ThreadSetup threadSetup; // instantiate this if you're talking to Cinder from a secondary thread
    
    while( ( ! mShouldQuit ) && (mCurrentProject) && ( ! mCurrentProject->mImages.empty() ) ) {
        
        if(mCurrentProject){
            try {
                console() << "Loading: " << mCurrentProject->mImages.back() << std::endl;
                mSurfaces->pushFront( Surface::create(loadImage( mCurrentProject->mImages.back() )) );
            }
            catch( ... ) {
                // just ignore any exceptions
            }
            mCurrentProject->mImages.pop_back();
        }
        
    }
}

void AtriumDisplayApp::mouseDown( MouseEvent event )
{
    
}

void AtriumDisplayApp::update()
{
    
    if( mMovie )
		mMovieFrameTexture = mMovie->getTexture();
    
    
    if(gTriggerTransition){
        mTransitionState = mTransitionStateNext;
        gTriggerTransition = false;
        switch (mTransitionStateNext) {
            case 0: // start and show lab name
                
                if(mThread.get()){
                    mThread->join();
                }
                // create and launch the thread
                mTaglineStringPos = (mTaglineStringPos+1)%mTaglineStrings.size();
                loadNextProject();
                mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
                
                timeline().apply( &mStripesSquareness, 0.0f, 0.f,EaseOutQuad() );
                timeline().apply( &mStripesPosition, vec2(1,0), 0.f,EaseInQuad() );
                timeline().apply( &mStripesNoise, .0f, .5f,EaseOutQuad() );
                timeline().apply( &mStripesFade, .9f, .5f,EaseInOutQuad() );
                timeline().apply( &mHeaderFade, 0.f, .5f,EaseInQuad() );
                timeline().apply( &mProjectTitleFade, 0.f, 1.0f,EaseInQuad() );
                timeline().apply( &mProjectDetailsFade, 0.f, 1.0f,EaseInQuad() );
                timeline().appendTo( &mStripesPosition, vec2(0,0), 8.f,EaseOutCubic() );
                timeline().appendTo( &mStripesNoise, .5f, 5.f,EaseInOutSine() ).delay(7.5f);
                timeline().apply( &mTitleFade, 1.0f, 4.f,EaseOutExpo() ).delay(6.f);
                timeline().appendTo( &mStripesFade, .6f, 4.f, EaseInOutQuad() ).delay(5.5f);
                timeline().appendTo( &mStripesNoise, 1.0f, 1.5f,EaseInQuad() ).finishFn(triggerTransition);
                timeline().appendTo( &mStripesFade, 1.f, 2.f, EaseInOutQuad() ).delay(1.5f);
                timeline().appendTo( &mTitleFade,  1.0f, 0.0f, 1.5f, EaseOutSine()).delay(3.f);
                timeline().appendTo( &mStripesSquareness, 1.0f, 4.5f,EaseInOutQuad() ).delay(12.f);
                mTransitionStateNext = 1;
                break;
            case 1: // show lab taglines
                timeline().apply( &mHeaderFade, 1.f, .5f,EaseInQuad() );
                timeline().apply( &mLogoFade, 1.f, .5f,EaseInQuad() );
                timeline().apply( &mStripesFade, 1.0f, 1.5f,EaseInOutQuad() );
                timeline().apply( &mStripesSquareness, 0.f, 5.0f,EaseInOutQuad() ).delay(4.f);
                timeline().apply( &mStripesPosition, vec2(-2,0), 5.f,EaseInQuad() ).delay(5.5f).finishFn( triggerTransition );
                timeline().apply( &mStripesNoise, .0f, 5.0f,EaseInOutSine() ).delay(3.5f);
                timeline().appendTo( &mHeaderFade, 0.f, 1.5f,EaseInQuad() ).delay(5.5f) ;
                timeline().appendTo( &mLogoFade, 0.f, 1.f,EaseInQuad() ).delay(12.f) ;
                mTransitionStateNext = 4;
                break;
                
            case 3: // movie player
                if( mCurrentProject && !mCurrentProject->mMovies.empty() ) {
                    if(!mMovie || mMovie->isDone() || !mMovie->isPlaying() ){
                        loadMovieFile(mCurrentProject->mMovies.back());
                        mCurrentProject->mMovies.pop_back();
                        if(mFadedTexture != &mLeftTexture) mLeftTexture.fadeToSurface(2.f);
                        timeline().apply( &mMovieFade, 1.f, 2.f,EaseInSine() ).delay(.5f);
                        timeline().add(triggerTransition, getElapsedSeconds()+mMovie->getDuration()-1.5f );
                    } else {
                        mMidTexture.fadeToSurface(0);
                        timeline().apply( &mMovieFade, .0f, 1.5f,EaseInSine() );
                        timeline().add(triggerTransition, getElapsedSeconds()+randFloat(3.5f,5.f));
                        mTransitionStateNext = 4;
                    }
                } else {
                    timeline().apply( &mMovieFade, .0f, 1.5f,EaseInSine() );
                    timeline().add(triggerTransition, getElapsedSeconds()+randFloat(3.5f,5.f));
                    mTransitionStateNext = 4;
                }
                break;
                
            case 4: // show slideshow
                
                // movies at odd fades from fade count 3
                if( (mFadedTextureFadeCount > 2 && mFadedTextureFadeCount % 2 == 1 )  && !mCurrentProject->mMovies.empty()){
                    mTransitionStateNext = 3;
                    mFadedTextureFadeCount++;
                    triggerTransition();
                    break;
                }
                
                if(mMovie) mMovie.reset();
                if(mMovieFrameTexture) mMovieFrameTexture.reset();
                
                // now it's slides
                
                if( mSurfaces->isNotEmpty() ) {
                    
                    SurfaceRef croppedSurface, newSurface;
                    mSurfaces->popBack( &newSurface );
                    FadingTexture * fadingTexture;
                    int whichTexture = randInt(4);
                    
                    // overrides of random values for deterministic start
                    
                    if ( mFadedTextureFadeCount == 0) whichTexture = 3;
                    if ( mFadedTextureFadeCount == 1) whichTexture = 1;
                    if ( mFadedTextureFadeCount == 2) whichTexture = 2;
                    if ( mFadedTextureFadeCount == 3) whichTexture = 1;
                    if ( mFadedTextureFadeCount == 4) whichTexture = 0;
                    
                    if(whichTexture == 0) fadingTexture = &mLeftTexture;
                    if(whichTexture == 1) fadingTexture = &mMidTexture;
                    if(whichTexture == 2) fadingTexture = &mRightTexture;
                    if(whichTexture == 3) fadingTexture = &mFullTexture;
                    
                    croppedSurface = Surface::create(newSurface->clone(fadingTexture->mBounds.proportionalFit(fadingTexture->mBounds, newSurface->getBounds(), true, true)));
                    
                    if (mFadedTexture == &mFullTexture && fadingTexture != &mFullTexture) {
                        // when fading down from full texture, set the new texture to black before fading it up.
                        fadingTexture->fadeToSurface(0.f); // show
                        mFullTexture.fadeToSurface(2.f);
                    }
                    
                    fadingTexture->fadeToSurface(croppedSurface);
                    if(mFadedTextureFadeCount < 2){
                        timeline().add(triggerTransition, getElapsedSeconds()+5.f);
                    } else {
                        timeline().add(triggerTransition, getElapsedSeconds()+randFloat(3.5f,5.f));
                    }
                    mFadedTexture = fadingTexture;
                    
                    if(mFadedTextureFadeCount == 0){
                        timeline().apply( &mProjectTitleFade, 1.f, 2.0f,EaseInOutSine() );
                    }
                    if(mFadedTextureFadeCount == 1){
                        timeline().apply( &mProjectDetailsFade, 1.f, 2.0f,EaseInOutSine() );
                    }
                    
                    mFadedTextureFadeCount++;
                    
                } else {
                    
                    if( !mCurrentProject->mMovies.empty()){
                        timeline().apply( &mProjectTitleFade, 1.f, 2.0f,EaseInOutSine() );
                        timeline().apply( &mProjectDetailsFade, 1.f, 2.0f,EaseInOutSine() );
                        mTransitionStateNext = 3;
                        triggerTransition();
                    } else {
                        mTransitionStateNext = -1;
                        timeline().add(triggerTransition, getElapsedSeconds()+.1f);
                    }
                }
                break;
            case -1: // next project
                
                mStripesSquareness = 0;
                mStripesNoise = 0;
                mStripesFade = 0;
                mStripesPosition = vec2(1,0);
                mFadedTextureFadeCount = 0;
                mTransitionStateNext = 0;
                if (mFadedTexture == &mFullTexture) {
                    mProjectDetailsFade = 0;
                    mLeftTexture.fadeToSurface(0);
                    mMidTexture.fadeToSurface(0);
                    mRightTexture.fadeToSurface(0);
                    mFullTexture.fadeToSurface(2.f);
                } else {
                    if(mLeftTexture.mFade > 0)
                        mProjectDetailsFade = 0;
                    mLeftTexture.fadeToSurface(1.f);
                    mMidTexture.fadeToSurface(1.5f);
                    mRightTexture.fadeToSurface(2.f);
                }
                timeline().apply( &mProjectTitleFade, 0.f, 1.f,EaseInQuad() );
                timeline().apply( &mProjectDetailsFade, 0.f, 1.f,EaseInQuad() );
                timeline().add(triggerTransition, getElapsedSeconds()+2.5);
                
                break;
        }
    }
}

void AtriumDisplayApp::draw()
{
    gl::ScopedBlendAlpha  blendAlpha;
	gl::clear();
    gl::color(1.,1.,1.,1.);
    
    float margin = getWindowHeight()/8.f;
    
    // PROJECT DETAILS
    
    Surface8u renderedHeader;
    vec2 headerMeasure;
    
    if(mProjectDetailsFade > 0 || mProjectTitleFade > 0){
        
        gl::pushMatrices();
        
        TextBox headerBox;
        headerBox.setSize(ivec2((getWindowWidth()/3.)-(2.5*margin), getWindowHeight()-(4*margin) ));
        headerBox.setColor(ColorA(1.,1.,1.,1.));
        headerBox.setFont(mHeaderFont);
        headerBox.setText(mCurrentProject->mTitle);
        renderedHeader = headerBox.render();
        headerMeasure = headerBox.measure();
        
        gl::translate(0,headerMeasure.y+(margin*.375));
        
        // tags
        
        vec2 tagOffset = vec2(0,0);
        
        for(int i = 0; i < mCurrentProject->mTags.size(); i++ ){
            if(boost::to_upper_copy( mCurrentProject->mTags[i] ) != "FEATURED"){
                TextBox tagsBox;
                tagsBox.setColor(ColorA(0.,0.,0.,1.));
                tagsBox.setFont(mTagFont);
                tagsBox.setText(boost::to_upper_copy( mCurrentProject->mTags[i] ));
                Surface8u renderedTag = tagsBox.render();
                vec2 tagMeasure = tagsBox.measure();
                
                gl::pushMatrices();
                gl::translate(margin*.25, 0.);
                gl::translate(tagOffset);
                gl::color(1,1,1,mProjectDetailsFade*.75);
                gl::drawSolidRect(Rectf(margin,margin,tagMeasure.x+(margin*1.25),tagMeasure.y+(margin*1.0625)) );
                gl::color(1.,1.,1.,mProjectDetailsFade);
                gl::draw(  gl::Texture::create( renderedTag ), vec2(margin*1.125, margin*1.03125) );
                gl::popMatrices();
                
                tagOffset.x += tagMeasure.x+(margin*.375f);
                if(tagOffset.x > (getWindowWidth()/3.)-(3*margin)){
                    tagOffset.y+=mTagFont.getAscent()+mTagFont.getDescent()+(margin*.2);
                    tagOffset.x = 0;
                }
            }
        }
        
        if(tagOffset.x > 0.)
            tagOffset.y += mTagFont.getAscent()+mTagFont.getDescent()+(margin*.25);
        
        gl::translate(0, tagOffset.y);
        
        // summary
        
        TextBox summaryBox;
        summaryBox.setSize(ivec2((getWindowWidth()/3.)-(2.5*margin), getWindowHeight()-(2.5*margin)-headerMeasure.y));
        summaryBox.setColor(ColorA(1.,1.,1.,1.));
        summaryBox.setFont(mParagraphFont);
        summaryBox.setText(mCurrentProject->mSummary);
        Surface8u renderedSummary = summaryBox.render();
        vec2 summaryMeasure = summaryBox.measure();
        // show background box when there's a full texture
        /*
         gl::color(0.1,0.1,0.1,mFullTexture.mFade*.75*mProjectTextFade);
         gl::drawSolidRect(Rectf(margin,margin,summaryMeasure.x+(margin*1.5),summaryMeasure.y+(margin*1.25)) );
         */
        gl::color(1.,1.,1.,mProjectDetailsFade);
        gl::draw(  gl::Texture::create( renderedSummary ), vec2(margin*1.25, margin*1.125 ));
        
        gl::translate(0, summaryMeasure.y + (margin*.375));
        
        gl::popMatrices();

        // small text
        
        TextBox smallBox;
        smallBox.setSize(ivec2((getWindowWidth()/3.)-(2.5*margin), getWindowHeight()-(2.5*margin)-headerMeasure.y));
        smallBox.setColor(ColorA(1.,1.,1.,1.));
        smallBox.setFont(mSmallFont);
        
        if(!mCurrentProject->mDate.is_not_a_date()){
            boost::gregorian::date_facet* facet(new boost::gregorian::date_facet("%B %Y"));
            stringstream ss;
            ss.imbue(std::locale(std::cout.getloc(), facet));
            ss << mCurrentProject->mDate;
            smallBox.setText(ss.str());
        }
        
        if(mCurrentProject->mHomepageURL.str().size() > 7){
            smallBox.appendText(" | " + mCurrentProject->mHomepageURL.str());
        }
        for(int i = 0; i < mCurrentProject->mParticipants.size(); i++ ){
            smallBox.appendText(" | ");
            smallBox.appendText(mCurrentProject->mParticipants[i] );
        }
        
        Surface8u renderedSmall = smallBox.render();
        vec2 smallMeasure = smallBox.measure();
        
        gl::color(1.,1.,1.,mProjectDetailsFade);
        gl::draw(  gl::Texture::create( renderedSmall ), vec2(margin*1.25, getWindowHeight()-(smallMeasure.y+margin) ));
        
    }
    
    gl::color(1.,1.,1.,1.);
    
    mLeftTexture.draw();
    mMidTexture.draw();
    mRightTexture.draw();
    mFullTexture.draw();
    
    if(mStripesFade > 0){
        
        int numLayers = 3;
        
        gl::color(1.,.9,0., mStripesFade/(numLayers-1.f));
        
        float noiseX;
        float segmentWidth = getWindowWidth() / 6.f;
        
        for (int i = 0; i < numLayers; i++){
            
            vector<PolyLine2f> stripe;
            
            for (int j = 0; j < 3; j++){
                
                stripe.push_back( PolyLine2f() );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.12*.25, 4*(i+1)*(j+1), 2));
                noiseX = lerp(noiseX, noiseX-1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( vec2( lerp(segmentWidth,0.f,mStripesSquareness)+noiseX, 0 ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.19*.25, 4*(i+1)*(j+1), 1.5));
                noiseX = lerp(noiseX, noiseX+1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( vec2( (getWindowWidth()/3.)+noiseX, 0 ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.13*.25, 4*(i+1)*(j+1), 37));
                noiseX = lerp(noiseX, noiseX+1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( vec2( ((getWindowWidth()/3.)-lerp(segmentWidth,0.f,mStripesSquareness))+noiseX, getWindowHeight() ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.15*.25, 4*(i+1)*(j+1), 3.33));
                noiseX = lerp(noiseX, noiseX-1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( vec2( noiseX, getWindowHeight()) );
                
                
            }

            gl::color(1., .9,.0, mStripesFade/(numLayers-1.f));

            Color yellow(1., .9, .0);
            Color red(1., .0, .0);
            Color blue(0., .4, .75);
            
            time_t timeSinceEpoch = time( NULL );
            struct tm *now = localtime( &timeSinceEpoch );
            
            if(now->tm_mon == 11){
                // it's december
                gl::color(yellow.r, easeOutCubic(1.0-mStripesNoise)*yellow.g,.0, mStripesFade/(numLayers-1.f));
            } else {
                gl::color(1., .0,.0, mStripesFade/(numLayers-1.f));
            }

            if(i==0){
                gl::color(lerp(yellow.r, blue.r, easeOutExpo(mStripesNoise)),
                          lerp(yellow.g, blue.g, easeOutExpo(mStripesNoise)),
                          lerp(yellow.b, blue.b, easeOutExpo(mStripesNoise)), mStripesFade);
            }
            gl::pushMatrices();
            gl::translate(((vec2)mStripesPosition).x * getWindowWidth()*(1.f+(i/numLayers)), ((vec2)mStripesPosition).y * getWindowHeight());
            gl::drawSolid(stripe.at(0));
            gl::translate(getWindowWidth()/3., 0);
            gl::drawSolid(stripe.at(1));
            gl::translate(getWindowWidth()/3., 0);
            gl::drawSolid(stripe.at(2));
            gl::popMatrices();
            
            if(now->tm_mon == 11){
                // Snowflake stuff
                
                float snowScale = getWindowHeight()*0.04;
                
                if(mSnowflakes.size() < 150) {
                    if(randFloat() < 0.1){
                        mSnowflakes.push_back(Snowflake());
                        mSnowflakes.back().radius = (0.4*snowScale) + randFloat(snowScale);
                        mSnowflakes.back().position.x = randFloat(getWindowWidth());
                        mSnowflakes.back().position.y = - (mSnowflakes.back().radius);
                        mSnowflakes.back().speed.x = randFloat(-1.0, 1.0);
                        mSnowflakes.back().speed.y = randFloat(0.0, 1.0);
                    }
                }
                
                int iSnowflake = 0;
                
                float snowNoiseTime = getElapsedSeconds()*.1;
                
                for (Snowflake & sf : mSnowflakes) {
                    vec2 forces(perlin.noise(snowNoiseTime, iSnowflake++*.005),
                                1.0);

                    sf.speed *= 0.975;
                    sf.speed.x = forces.x * snowScale * 0.5 / sf.radius;
                    sf.speed.y = forces.y * snowScale * 0.5 / sf.radius;
                    sf.speed.x += ((vec2)mStripesPosition).x * 1.75 * sf.radius;
                    sf.position.x += sf.speed.x;
                    sf.position.y = sf.position.y + sf.speed.y;
                    
                    
                    if(sf.position.y > getWindowHeight() + sf.radius){
                        sf.position.x = randFloat(getWindowWidth());
                        sf.position.y = - sf.radius ;
                        sf.speed.x = randFloat(-1, 1);
                        sf.speed.y = randFloat(0, 1);
                    }
                    
                    gl::color(1,1,1,easeInOutCubic(mStripesNoise)*0.9);
                    gl::drawSolidCircle(sf.position, sf.radius);
                }
                
            }
            
        }
    }
    
    // PROJECT TITLE
    
    if(mProjectTitleFade > 0){
        gl::pushMatrices();
        
        // show background box when there's a full texture
        gl::color(0.1,0.1,0.1,fmaxf(mFullTexture.mFade,mLeftTexture.mFade)*.75*mProjectTitleFade);
        gl::drawSolidRect(Rectf(margin,margin,headerMeasure.x+(margin*1.5),headerMeasure.y+margin));
        gl::color(1.,1.,1.,mProjectTitleFade);
        gl::draw(  gl::Texture::create( renderedHeader ), vec2(margin*1.25, margin));
        
        gl::popMatrices();
        
    }
    
    // MOVIE PLAYER
    
    if(mMovieFade > 0){
        
        if( mMovieFrameTexture ) {

        gl::color( 0.05, 0.05, 0.05, mMovieFade*.75 );
            
        gl::drawSolidRect(Rectf(getWindowWidth()/3.f, 0, getWindowWidth(), getWindowHeight()));
        
            // movie 

            Rectf movieRect = Rectf(getWindowWidth()/3.f, 0, getWindowWidth()*2.f/3.f, (getWindowWidth()/3.f)/mMovieFrameTexture->getAspectRatio());
            
            gl::color( mTintColor.r, mTintColor.g, mTintColor.b, mMovieFade );
            
            gl::draw( mMovieFrameTexture, movieRect);
            {
                gl::ScopedBlendAdditive  blendAdditive;
                gl::color( 1.0-mTintColor.r, 1.0-mTintColor.g, 1.0-mTintColor.b, mMovieFade*.5 );
                gl::drawSolidRect(movieRect);
            }
        
        // duration clock
        
        Rectf timeLineRect = Rectf(mLogoTexture->getBounds());
        timeLineRect.offset(vec2((getWindowWidth()*2.f/3.f)+margin, getWindowHeight()-(margin+timeLineRect.getHeight())) );
        
        float timeOffset = (mMovie->getCurrentTime()/mMovie->getDuration());
        
        gl::color( .1f, .1f, .1f, .9f*mMovieFade);
        
        gl::drawSolidRect(timeLineRect);
            Area vp(timeLineRect.getOffset(vec2(0,-(timeLineRect.y1-margin)) ));
            gl::pushViewport(vp.getUL(), vp.getSize());
        gl::pushMatrices();
        gl::scale(getWindowWidth()*1.0f/timeLineRect.getWidth(),getWindowHeight()*1.0f/timeLineRect.getHeight() );

        vector<PolyLine2f> vedges;
        float segmentWidth = timeLineRect.getHeight() * (16.f/9.f) * .5f;
        
        vedges.push_back( PolyLine2f() );
        vedges.back().push_back( vec2( segmentWidth , 0 ) );
        vedges.back().push_back( vec2( segmentWidth*2.f , 0 ) );
        vedges.back().push_back( vec2( segmentWidth, timeLineRect.getHeight()) );
        vedges.back().push_back( vec2( 0, timeLineRect.getHeight()) );

            gl::translate(lerp( -segmentWidth, timeLineRect.getWidth()-(segmentWidth*6.f),timeOffset),0.f);
            for(float x = 0; x < timeLineRect.getWidth(); x+=segmentWidth*2.f){
                gl::color( 1.f, .9f, .0f, mMovieFade);
                gl::drawSolid(vedges.at(0));
                gl::translate(-segmentWidth,0);
                gl::color( .0f, .0f, .0f, mMovieFade);
                gl::drawSolid(vedges.at(0));
                gl::translate(-segmentWidth,0);
            }

        gl::popMatrices();
        
        gl::popViewport();
        
        gl::color( 1.f, 1.f, 1.f, mMovieFade);

            float inverseDuration = mMovie->getDuration() - mMovie->getCurrentTime();
            
         TextBox movieBox;
         movieBox.setSize(ivec2((getWindowWidth()/3.)-(2*margin), getWindowHeight()-(2*margin) ));
         movieBox.setFont( mHeaderFont );
         movieBox.setColor(ColorA(1.,1.,1.,1.));
         movieBox.setText(str( (boost::format("%1$02d:%2$02d:%3$02d") % floor(inverseDuration/60.f) % floor(fmodf(inverseDuration,60.f)) % floor(fmodf(inverseDuration,1.f)*mMovie->getFramerate()) )));
         vec2 movieMeasure = movieBox.measure();
         Surface8u rendered = movieBox.render();
            gl::draw(  gl::Texture::create( rendered ), vec2((getWindowWidth()-margin)-movieMeasure.x, (timeLineRect.getY1()+((timeLineRect.getHeight()-movieMeasure.y)/2.f))));
        }
    }
    
    // SECTION HEADERS
    
    if(mHeaderFade > 0){
        gl::color(0.,0.,0.,mHeaderFade);
        gl::pushMatrices();
        
        TextBox headerBox;
        headerBox.setSize(ivec2((getWindowWidth()/3.)-(2*margin), getWindowHeight()-(2*margin) ));
        headerBox.setFont( mHeaderFont );
        headerBox.setColor(ColorA(0.,0.,0.,1.));
        headerBox.setText(mTaglineStrings.at(mTaglineStringPos));
        
        vec2 headerMeasure = headerBox.measure();
        
        Surface8u rendered = headerBox.render();
        gl::draw(  gl::Texture::create( rendered ), vec2(margin, getWindowHeight()-(margin+headerMeasure.y)+mHeaderFont.getDescent()));
        
        gl::popMatrices();
        
    }
    
    // ITU LOGO
    
    if(mLogoFade > 0){
        
        gl::color(1.,1.,1.,mLogoFade);
        gl::draw( mLogoTexture, vec2( (getWindowWidth()*2.f/3.f)+margin, (getWindowHeight()-margin)-mLogoTexture->getHeight() ) );
        
    }
    
    // INTERMEDIA LAB TITLE
    
    gl::color(1.,1.,1.,mTitleFade);
    vec2 stringDims = mTitleFontPrimary->measureString( "INTER" );
    mTitleFontPrimary->drawString( "INTER", vec2((getWindowWidth()/3.)-(stringDims.x+20), getWindowHeight()*0.65) );
    mTitleFontPrimary->drawString( "MEDIA", vec2((getWindowWidth()/3.)+10, getWindowHeight()*0.65) );
    mTitleFontSecondary->drawString( "LAB", vec2((getWindowWidth()*2/3.)+10, getWindowHeight()*0.65) );
    
    
    // SCREEN SEPERATOR BORDERS
    
    gl::color(.3,.3,.3);
    gl::drawLine(vec2(getWindowWidth()/3., 0), vec2(getWindowWidth()/3.,getWindowHeight()));
    gl::drawLine(vec2(getWindowWidth()*2/3., 0), vec2(getWindowWidth()*2/3.,getWindowHeight()));
    
}

bool AtriumDisplayApp::readConfig(){
    
    // load configuration file and find ressource path
    configYaml = YAML::LoadFile(Platform::get()->getResourcePath(RES_CUSTOM_YAML_CONFIG).c_str());
    if(configYaml["resourcePath"]){
        configResourcePath = fs::path(expand_user(configYaml["resourcePath"].as<std::string>()));
        
        if(fs::exists(configResourcePath) && fs::is_directory(configResourcePath)){
            
            typedef vector<fs::path> paths;         // store paths,
            paths resPaths;                         // so we can sort them later
            
            copy(fs::directory_iterator(configResourcePath), fs::directory_iterator(), back_inserter(resPaths));
            
            sort(resPaths.begin(), resPaths.end()); // sort, since directory iteration
            // is not ordered on some file systems
            
            for (paths::const_iterator resIt (resPaths.begin()); resIt != resPaths.end(); ++resIt)
            {
                
                if ( fs::is_directory(*resIt) ){
                    if (resIt->filename() == "projects" ){
                        
                        // projects are loaded in reverse cronological order
                        
                        paths prjPaths;
                        copy(fs::directory_iterator(*resIt), fs::directory_iterator(), back_inserter(prjPaths));
                        sort(prjPaths.begin(), prjPaths.end());
                        reverse(prjPaths.begin(), prjPaths.end());
                        
                        mProjects.clear();
                        mCurrentProject = NULL;
                        
                        for (paths::const_iterator prjIt (prjPaths.begin()); prjIt != prjPaths.end(); ++prjIt)
                        {
                            if (fs::is_directory(*prjIt)) {
                                
                                if(! boost::starts_with(prjIt->filename().string(), "_")) {
                                    
                                    Project *p = new Project(*prjIt);
                                    
                                    mProjects.push_back(p);
                                    
                                }
                                
                            }
                        }
                        
                    }
                    
                } else {
                    if (resIt->filename() == "lab.yaml"){
                        
                        // user-defined configuration
                        
                        YAML::Node labYaml = YAML::LoadFile(resIt->c_str());
                        
                        if (labYaml["taglines"]) {
                            
                            // taglines
                            
                            mTaglineStrings.clear();
                            mTaglineStringPos = 0;
                            
                            for(YAML::iterator tagIt=labYaml["taglines"].begin();tagIt!=labYaml["taglines"].end();++tagIt) {
                                mTaglineStrings.push_back(tagIt->as<std::string>());
                            }
                        }
                        
                    }
                    
                }
                
            }
            
            return true;
            
        } else {
            console() << "Missing folder: " << configResourcePath << endl;
        }
    } else {
        console() << "No config file at: " << configResourcePath << endl;
    }
    
    return false;
    
}

void AtriumDisplayApp::loadNextProject(){
    
    // console() << "loadNextProject" << endl;
    
    if(mCurrentProject){
        // console() << "Former project was: " + mCurrentProject->mTitle << endl;
        mProjects.push_back(mCurrentProject);
        mProjects.pop_front();
    }
    mCurrentProject = mProjects.front();
    // console() << "Next project is: " + mCurrentProject->mTitle << endl;
    mCurrentProject->reload();
}

void AtriumDisplayApp::loadMovieFile( const fs::path &moviePath )
{
    
    try {
		// load up the movie, set it to loop, and begin playing
		mMovie = qtime::MovieGl::create( moviePath );
		mMovie->play();
		
		// create a texture for showing some info about the movie
		TextLayout infoText;
		infoText.clear( ColorA( 0.2f, 0.2f, 0.2f, 0.5f ) );
		infoText.setColor( Color::white() );
		infoText.addCenteredLine( moviePath.filename().string() );
		infoText.addLine( toString( mMovie->getWidth() ) + " x " + toString( mMovie->getHeight() ) + " pixels" );
		infoText.addLine( toString( mMovie->getDuration() ) + " seconds" );
		infoText.addLine( toString( mMovie->getNumFrames() ) + " frames" );
		infoText.addLine( toString( mMovie->getFramerate() ) + " fps" );
		infoText.setBorder( 4, 2 );
        mMovieInfoTexture = gl::Texture::create( infoText.render( true ) );
        
        try {
            
            std::string movieSubtitleJsPath = moviePath.generic_string() + ".js";
            if(fs::exists(movieSubtitleJsPath)){
//                JsonTree subtitles(loadFile(movieSubtitleJsPath));
                console() << loadFile(movieSubtitleJsPath) << endl;
            }
        }
        catch( ... ) {
            
            console() << "Unable to load the subtitles." << std::endl;
        }
	}
	catch( ... ) {
		console() << "Unable to load the movie." << std::endl;
		mMovie.reset();
		mMovieInfoTexture.reset();
	}
    
	mMovieFrameTexture.reset();
    
}


void AtriumDisplayApp::shutdown()
{
    mShouldQuit = true;
    mSurfaces->cancel();
    mThread->join();
}

CINDER_APP( AtriumDisplayApp, RendererGl(), [&]( App::Settings *settings ) {
    settings->setWindowSize(Display::getDisplays()[0]->getWidth(), round(Display::getDisplays()[0]->getWidth()*(9./(16*3))));
    settings->setFullScreen( false );
    settings->setResizable( false );
    settings->setPowerManagementEnabled(false);
    settings->setAlwaysOnTop();
    
    for(int i = 0; i < Display::getDisplays().size(); i++){
        shared_ptr<Display> d = Display::getDisplays()[i];
        if(abs((d->getHeight()*1.f/d->getWidth())-(9.f/(16.f*3.f))) < 0.1) {
            //it's the tripplehead
            settings->setDisplay(d);
            settings->setWindowSize(d->getSize());
            settings->setFullScreen( true );
            //            settings->setFrameRate(30.f);
        }
    }
})
