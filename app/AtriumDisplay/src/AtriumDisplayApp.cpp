#include "cinder/app/AppNative.h"
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
#include "Resources.h"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem.hpp>
#include <boost/date_time/gregorian/gregorian.hpp>
#include <string>
#include <cstdio> // for std::remove

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
    void fadeToSurface(Surface newSurface, float duration=1.5f);
    void fadeToSurface(float duration=1.5f);
    
    Anim<float>     mCrossFade;
    Anim<float>     mFade;
    Area            mBounds;
    Color           mColor;
    gl::Texture     mTexture, mLastTexture;
    
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
		Rectf textureBounds = mLastTexture.getBounds();
		drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mLastTexture, drawBounds );
    }
	if( mTexture ) {
		gl::color( mColor.r, mColor.g, mColor.b, mCrossFade*mFade );
		Rectf textureBounds = mTexture.getBounds();
		drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mTexture, drawBounds );
	}
    gl::enableAdditiveBlending();
    gl::color( 1.0-mColor.r, 1.0-mColor.g, 1.0-mColor.b, mFade*.9 );
    gl::drawSolidRect(drawBounds);
    gl::enableAlphaBlending();
    
}

void FadingTexture::fadeToSurface(float duration){
    timeline().apply( &mFade, 0.0f, duration ).finishFn(ResetFadingTextureFunctor( this ));
}

void FadingTexture::fadeToSurface(Surface newSurface, float duration){
    
    if( newSurface ) {
        mLastTexture = mTexture; // the "last" texture is now the current text
        
        mTexture = gl::Texture( newSurface );
        
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
    
    Project(fs::path p);
    
    void loadYAMLFile(fs::path pYAML);
    void setupResources(fs::path p);
    void reload();
    
    fs::path            mPath;
    vector<fs::path>    mResources;
    
//    ConcurrentCircularBuffer<Surface> *mSurfaces;

    boost::gregorian::date
                        mDate;
    std::string         mTitle;
    std::string         mAbstract;
    std::string         mSummary;
    vector<std::string> mParticipants;
    vector<std::string> mCredits;
    vector<std::string> mMaterials;
    vector<std::string> mTags;
    std::string         mCreativeCommons;
    Url                 mURL;
    
};

Project::Project(fs::path p){

 //   mSurfaces = new ConcurrentCircularBuffer<Surface>( 5 ); // room for 5 images

    if(fs::exists(p) && fs::is_directory(p)){
        mPath = p;
        reload();
    }
}

void Project::reload(){
    if(fs::exists(mPath) && mPath != ""){
        mResources.clear();
        setupResources(mPath);
        fs::path pYAML = fs::path(mPath.string() + "/project.yaml");
        if(fs::exists(pYAML)){
            loadYAMLFile(pYAML);
        }
    }
}

void Project::loadYAMLFile(fs::path pYAML){
    
    // load YAML file
    YAML::Node projectYaml = YAML::LoadFile(pYAML.c_str());

    if(projectYaml["date"]){
        try {
            mDate = boost::gregorian::from_undelimited_string(projectYaml["date"].as<std::string>());
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
        YAML::Node n = projectYaml["participants"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mParticipants.push_back((*it)["name"].as<std::string>());
    }

    if(projectYaml["credits"]){
        YAML::Node n = projectYaml["credits"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mCredits.push_back((*it)["name"].as<std::string>());
    }

    if(projectYaml["materials"]){
        YAML::Node n = projectYaml["materials"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mMaterials.push_back(it->as<std::string>());
    }

    if(projectYaml["tags"]){
        YAML::Node n = projectYaml["tags"];
        for(YAML::const_iterator it=n.begin();it!=n.end();++it)
            mTags.push_back(it->as<std::string>());
    }
    
    if(projectYaml["url"]){
        mURL = Url(projectYaml["url"].as<std::string>());
    }
    
    //std::string         mCreativeCommons;

    
}

void Project::setupResources(fs::path p){
    
    if(fs::exists(p) && fs::is_directory(p)){
        
        typedef vector<fs::path> paths;         // store paths,
        paths resPaths;                         // so we can sort them later
        
        copy(fs::directory_iterator(p), fs::directory_iterator(), back_inserter(resPaths));
        
        sort(resPaths.begin(), resPaths.end()); // sort, since directory iteration
        // is not ordered on some file systems
        
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
            }
            
            if(boost::iequals(resIt->extension().string(), ".jpg") ||
               boost::iequals(resIt->extension().string(), ".png") ||
               boost::iequals(resIt->extension().string(), ".gif") ||
               boost::iequals(resIt->extension().string(), ".jpeg") ){
                //image files
                mResources.push_back(*resIt);
            }
        }
    }
    
}


#pragma mark AtriumDisplayApp

class AtriumDisplayApp : public AppNative {
public:
	void prepareSettings( Settings *settings );
	void setup();
	void mouseDown( MouseEvent event );
	void update();
	void draw();
    void loadNextProject();
	void shutdown();
    
    bool readConfig();
    
    void loadImagesThreadFn();
    
    ConcurrentCircularBuffer<Surface>	*mSurfaces;
    
    deque<Project*>        mProjects;
    
    Project                 *mCurrentProject;
    
    gl::Texture	mTitleTexture, mHeaderTexture, mProjectTexture, mLogoTexture;
    
    gl::TextureFontRef      mTitleFontPrimary;
    gl::TextureFontRef      mTitleFontSecondary;
    
    Font                    mHeaderFont;
    Font                    mParagraphFont;
    
	bool					mShouldQuit;
	shared_ptr<thread>		mThread;
	FadingTexture			mFullTexture, mLeftTexture, mMidTexture, mRightTexture;
    FadingTexture *         mFadedTexture;
	Anim<float>				mFade;
    Anim<float>             mTitleFade;
    Anim<float>             mStripesNoise;
    Anim<Vec2f>             mStripesPosition;
    Anim<float>             mStripesFade;
    Anim<float>             mStripesSquareness;
    int                     mTransitionState;
    int                     mTransitionStateNext;
	double					mLastTime;
    Anim<float>             mLogoFade;
    Anim<float>             mHeaderFade;
    Anim<float>             mProjectTextFade;
    int                     mTaglineStringPos;
    vector<string>          mTaglineStrings;
    
    YAML::Node              configYaml;
    
    fs::path                configResourcePath;
    
    Perlin                  perlin;
};

void AtriumDisplayApp::prepareSettings( Settings *settings )
{
    
    settings->setWindowSize(Display::getDisplays()[0]->getWidth(), round(Display::getDisplays()[0]->getWidth()*(9./(16*3))));
	settings->setFullScreen( false );
	settings->setResizable( false );
    
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
}

void AtriumDisplayApp::setup()
{
    hideCursor();
    
    srandomdev();
    randSeed(random());
    perlin.setSeed(randInt());
    
	mShouldQuit = false;
	mSurfaces = new ConcurrentCircularBuffer<Surface>( 3 ); // room for 5 images
    
    // create and launch the thread
    // mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
    
    mFullTexture.mBounds = getWindowBounds();
    mLeftTexture.mBounds.set(0, 0, getWindowWidth()/3.f, getWindowHeight());
    mMidTexture.mBounds.set(getWindowWidth()/3.f, 0, getWindowWidth()*2.f/3.f, getWindowHeight());
    mRightTexture.mBounds.set(getWindowWidth()*2.f/3.f, 0, getWindowWidth(), getWindowHeight());
    
    mFullTexture.mColor = mLeftTexture.mColor = mMidTexture.mColor = mRightTexture.mColor = Color(1.f,.95f, .75f);
    
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
        mLogoTexture = gl::Texture( rendered );
    }
    
    {
        Font titleFontPrimary = Font( loadResource(RES_CUSTOM_FONT_BOLD), getWindowHeight()*0.4 );
        mTitleFontPrimary = gl::TextureFont::create( titleFontPrimary, f );
        
        Font titleFontSecondary = Font( loadResource(RES_CUSTOM_FONT_LIGHT), getWindowHeight()*0.4 );
        mTitleFontSecondary = gl::TextureFont::create( titleFontSecondary, f );
    }
    
    mHeaderFont = Font( loadResource(RES_CUSTOM_FONT_LIGHT), getWindowHeight()*0.1 );
    mParagraphFont = Font( loadResource(RES_CUSTOM_FONT_REGULAR), getWindowHeight()*0.05 );
    mLastTime = getElapsedSeconds();
    mTransitionState = 0; // app just started;
    mTransitionStateNext = 0; // app just started;
    mTitleFade = 0;
    mHeaderFade = 0;
    mLogoFade = 0;
    mProjectTextFade = 0;
    mStripesFade = 0;
    mStripesSquareness = 1;
    mStripesPosition = Vec2f(0,0);
    
    readConfig();
    
    triggerTransition();
    
}

void AtriumDisplayApp::loadImagesThreadFn()
{
	ci::ThreadSetup threadSetup; // instantiate this if you're talking to Cinder from a secondary thread

    while( ( ! mShouldQuit ) && (mCurrentProject) && ( ! mCurrentProject->mResources.empty() ) ) {
        
        if(mCurrentProject){
            try {
                console() << "Loading: " << mCurrentProject->mResources.back() << std::endl;
                mSurfaces->pushFront( loadImage(mCurrentProject->mResources.back()) );
                mCurrentProject->mResources.pop_back();
            }
            catch( ... ) {
                // just ignore any exceptions
            }
        }
        
    }
    
    
    

    /*
    
	// parse the image URLS from the XML feed and push them into 'urls'
	const Url sunFlickrGroup = Url( "http://api.flickr.com/services/feeds/photos_public.gne?tags=it,university,copenhagen&format=rss_200&tagmode=all" );
    console() << sunFlickrGroup.c_str() << endl;
	const XmlTree xml( loadUrl( sunFlickrGroup ) );
	for( XmlTree::ConstIter item = xml.begin( "rss/channel/item" ); item != xml.end(); ++item ) {
		const XmlTree &urlXml = ( ( *item / "media:content" ) );
		urls.push_back( Url( urlXml["url"] ) );
	}
    
	// load images as Surfaces into our ConcurrentCircularBuffer
	// don't create gl::Textures on a background thread
	while( ( ! mShouldQuit ) && ( ! urls.empty() ) ) {
		try {
			console() << "Loading: " << urls.back() << std::endl;
			mSurfaces->pushFront( loadImage( loadUrl( urls.back() ) ) );
			urls.pop_back();
		}
		catch( ... ) {
			// just ignore any exceptions
		}
	}
    */
}

void AtriumDisplayApp::mouseDown( MouseEvent event )
{
    
}

void AtriumDisplayApp::update()
{
    
    if(gTriggerTransition){
        mTransitionState = mTransitionStateNext;
        switch (mTransitionStateNext) {
            case 0: // start
                
                if(mThread.get()){
                    mThread->join();
                }
                // create and launch the thread
                mTaglineStringPos = (mTaglineStringPos+1)%mTaglineStrings.size();
                loadNextProject();
                mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
                
                timeline().apply( &mStripesSquareness, 0.0f, 0.f,EaseOutQuad() );
                timeline().apply( &mStripesPosition, Vec2f(1,0), 0.f,EaseInQuad() );
                timeline().apply( &mStripesNoise, .0f, .5f,EaseOutQuad() );
                timeline().apply( &mStripesFade, .9f, .5f,EaseInOutQuad() );
                timeline().apply( &mHeaderFade, 0.f, .5f,EaseInQuad() );
                timeline().apply( &mProjectTextFade, 0.f, 1.0f,EaseInQuad() );
                timeline().appendTo( &mStripesPosition, Vec2f(0,0), 8.f,EaseOutCubic() );
                timeline().appendTo( &mStripesNoise, .5f, 5.f,EaseInOutSine() ).delay(7.5f);
                timeline().apply( &mTitleFade, 1.0f, 4.f,EaseOutExpo() ).delay(6.f);
                timeline().appendTo( &mStripesFade, .6f, 4.f, EaseInOutQuad() ).delay(5.5f);
                timeline().appendTo( &mStripesNoise, 1.0f, 1.5f,EaseInQuad() ).finishFn(triggerTransition);
                timeline().appendTo( &mStripesFade, 1.f, 2.f, EaseInOutQuad() ).delay(1.5f);
                timeline().appendTo( &mTitleFade,  1.0f, 0.0f, 1.5f, EaseOutSine()).delay(3.f);
                timeline().appendTo( &mStripesSquareness, 1.0f, 4.5f,EaseInOutQuad() ).delay(12.f);
                mTransitionStateNext = 1;
                break;
            case 1:
                timeline().apply( &mHeaderFade, 1.f, .5f,EaseInQuad() );
                timeline().apply( &mLogoFade, 1.f, .5f,EaseInCubic() );
                timeline().apply( &mStripesFade, 1.0f, 1.5f,EaseInOutQuad() );
                timeline().apply( &mStripesSquareness, 0.f, 5.0f,EaseOutQuad() ).delay(4.f);
                timeline().apply( &mStripesPosition, Vec2f(-2,0), 5.f,EaseInQuad() ).delay(5.5f).finishFn( triggerTransition );
                timeline().apply( &mStripesNoise, .0f, 5.0f,EaseInOutSine() ).delay(3.5f);
                timeline().appendTo( &mHeaderFade, 0.f, 1.5f,EaseInQuad() ).delay(5.5f) ;
                timeline().appendTo( &mLogoFade, 0.f, 1.f,EaseInQuad() ).delay(12.f) ;
                mTransitionStateNext = 4;
                break;
            case 4:
                if( mSurfaces->isNotEmpty() ) {
                    
                    Surface croppedSurface, newSurface;
                    mSurfaces->popBack( &newSurface );
                    FadingTexture * fadingTexture;
                    int whichTexture = randInt(4);
                    
                    if(whichTexture == 0) fadingTexture = &mLeftTexture;
                    if(whichTexture == 1) fadingTexture = &mMidTexture;
                    if(whichTexture == 2) fadingTexture = &mRightTexture;
                    if(whichTexture == 3) fadingTexture = &mFullTexture;
                    
                    croppedSurface = newSurface.clone(fadingTexture->mBounds.proportionalFit(fadingTexture->mBounds, newSurface.getBounds(), true));
                    
                    if (mFadedTexture == &mFullTexture && fadingTexture != &mFullTexture) {
                        fadingTexture->fadeToSurface(0.f);
                        mFullTexture.fadeToSurface(2.f);
                    }
                    
                    fadingTexture->fadeToSurface(croppedSurface);
                    timeline().add(triggerTransition, getElapsedSeconds()+randFloat(3.5f,5.f));
                    
                    mFadedTexture = fadingTexture;
                    
                    timeline().apply( &mProjectTextFade, 1.f, 5.0f,EaseInOutSine() );
                    
                } else {
                    mStripesSquareness = 0;
                    mStripesNoise = 0;
                    mStripesFade = 0;
                    mStripesPosition = Vec2f(1,0);
                    mTransitionStateNext = 0;
                    if (mFadedTexture == &mFullTexture) {
                        mLeftTexture.fadeToSurface(0);
                        mMidTexture.fadeToSurface(0);
                        mRightTexture.fadeToSurface(0);
                        mFullTexture.fadeToSurface(2.f);
                    } else {
                        mLeftTexture.fadeToSurface(1.f);
                        mMidTexture.fadeToSurface(1.5f);
                        mRightTexture.fadeToSurface(2.f);
                    }
                    timeline().apply( &mProjectTextFade, 0.f, 1.0f,EaseInQuad() );
                    timeline().add(triggerTransition, getElapsedSeconds()+2.5);
                }
                break;
        }
        gTriggerTransition = false;
    }
}

void AtriumDisplayApp::draw()
{
	gl::enableAlphaBlending();
	gl::clear();
    
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
                stripe.back().push_back( Vec2f( lerp(segmentWidth,0.f,mStripesSquareness)+noiseX, 0 ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.19*.25, 4*(i+1)*(j+1), 1.5));
                noiseX = lerp(noiseX, noiseX+1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( Vec2f( (getWindowWidth()/3.)+noiseX, 0 ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.13*.25, 4*(i+1)*(j+1), 37));
                noiseX = lerp(noiseX, noiseX+1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( Vec2f( ((getWindowWidth()/3.)-lerp(segmentWidth,0.f,mStripesSquareness))+noiseX, getWindowHeight() ) );
                
                noiseX = (perlin.noise(getElapsedSeconds()*.15*.25, 4*(i+1)*(j+1), 3.33));
                noiseX = lerp(noiseX, noiseX-1.f, mStripesSquareness);
                noiseX *= mStripesNoise*segmentWidth*(5+i);
                stripe.back().push_back( Vec2f( noiseX, getWindowHeight()) );
                
                
            }
            
            gl::color(1., .9,.0, mStripesFade/(numLayers-1.f));
            if(i==0){
                gl::color(lerp(1.,0.,easeOutExpo(mStripesNoise)), lerp(.9,.4, easeOutExpo(mStripesNoise)),lerp(0.,.75,easeOutExpo(mStripesNoise)), mStripesFade);
            }
            gl::pushMatrices();
            gl::translate(((Vec2f)mStripesPosition).x * getWindowWidth()*(1.f+(i/numLayers)), ((Vec2f)mStripesPosition).y * getWindowHeight());
            gl::drawSolid(stripe.at(0));
            gl::translate(getWindowWidth()/3., 0);
            gl::drawSolid(stripe.at(1));
            gl::translate(getWindowWidth()/3., 0);
            gl::drawSolid(stripe.at(2));
            gl::popMatrices();
        }
    }
    
    // TEXT
    
    float margin = getWindowHeight()/8.f;

    // PROJECT TEXT
    
    if(mProjectTextFade > 0){
        gl::color(1.,1.,1.,mProjectTextFade);
        gl::pushMatrices();
        
        TextLayout layout;
        layout.clear(ColorA(0.,0.,0.,0.1));
        layout.setFont( mHeaderFont );
        layout.setColor(ColorA(1.,1.,1.,1.));
        layout.addLine(" " +mCurrentProject->mTitle + " ");
        Surface8u rendered = layout.render();
        gl::draw(  gl::Texture( rendered ), Vec2f(margin, margin));

        TextBox box;
        box.setSize(Vec2i((getWindowWidth()/3.)-(2*margin), getWindowHeight()-(4*margin) ));
        layout.setColor(ColorA(1.,1.,1.,1.));
        box.setFont(mParagraphFont);
        layout.addLine(mCurrentProject->mSummary);
        Surface8u renderedBox = box.render();
        gl::draw(  gl::Texture( renderedBox ), Vec2f(margin, (4*margin) ));

        
        gl::popMatrices();
        
    }
    
    // SECTION HEADERS
    
    if(mHeaderFade > 0){
        gl::color(0.,0.,0.,mHeaderFade);
        gl::pushMatrices();
        
        TextBox headerBox;
        headerBox.setSize(Vec2i((getWindowWidth()/3.)-(2*margin), getWindowHeight()-(2*margin) ));
        headerBox.setFont( mHeaderFont );
        headerBox.setColor(ColorA(0.,0.,0.,1.));
        headerBox.setText(mTaglineStrings.at(mTaglineStringPos));
        
        Surface8u rendered = headerBox.render();
        gl::draw(  gl::Texture( rendered ), Vec2f(margin, margin));
        
        //        mHeaderFont->drawStringWrapped(mTaglineStrings.at(mTaglineStringPos), Rectf(margin,margin, (getWindowWidth()/3.)-(2*margin), getWindowHeight()-(2*margin)));
        gl::popMatrices();
        
    }
    
    // ITU LOGO
    
    if(mLogoFade > 0){
        
        gl::color(1.,1.,1.,mLogoFade);
        gl::draw( mLogoTexture, Vec2f( (getWindowWidth()*2.f/3.f)+margin, (getWindowHeight()-margin)-mLogoTexture.getHeight() ) );
        
    }
    
    // INTERMEDIA LAB TITLE
    
    gl::color(1.,1.,1.,mTitleFade);
    Vec2f stringDims = mTitleFontPrimary->measureString( "INTER" );
    mTitleFontPrimary->drawString( "INTER", Vec2f((getWindowWidth()/3.)-(stringDims.x+10), getWindowHeight()*0.65) );
    mTitleFontPrimary->drawString( "MEDIA", Vec2f((getWindowWidth()/3.), getWindowHeight()*0.65) );
    mTitleFontSecondary->drawString( "LAB", Vec2f((getWindowWidth()*2/3.), getWindowHeight()*0.65) );
    
    
    // SCREEN SEPERATOR BORDERS
    
    gl::color(.3,.3,.3);
    gl::drawLine(Vec2f(getWindowWidth()/3., 0), Vec2f(getWindowWidth()/3.,getWindowHeight()));
    gl::drawLine(Vec2f(getWindowWidth()*2/3., 0), Vec2f(getWindowWidth()*2/3.,getWindowHeight()));
    
}

bool AtriumDisplayApp::readConfig(){
    
    // load configuration file and find ressource path
    
    configYaml = YAML::LoadFile(getResourcePath(RES_CUSTOM_YAML_CONFIG).c_str());
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

    console() << "loadNextProject" << endl;
    
    if(mCurrentProject){
        console() << "Former project was: " + mCurrentProject->mTitle << endl;
        mProjects.push_back(mCurrentProject);
        mProjects.pop_front();
    }
    mCurrentProject = mProjects.front();
    console() << "Next project is: " + mCurrentProject->mTitle << endl;
    mCurrentProject->reload();
}

void AtriumDisplayApp::shutdown()
{
    mShouldQuit = true;
    mSurfaces->cancel();
    mThread->join();
}

CINDER_APP_NATIVE( AtriumDisplayApp, RendererGl(RendererGl::AA_NONE) )
