#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/Xml.h"
#include "cinder/Timeline.h"
#include "cinder/ImageIo.h"
#include "cinder/Thread.h"
#include "cinder/Rand.h"
#include "cinder/ConcurrentCircularBuffer.h"
#include "cinder/gl/TextureFont.h"
#include "Resources.h"

using namespace ci;
using namespace ci::app;
using namespace std;

bool gTriggerTransition;

void triggerTransition(){
    gTriggerTransition = true;
}

class FadingTexture {
  public:
    FadingTexture();
    void draw();
    void fadeToSurface(Surface newSurface, float duration=1.5f);
    void fadeToSurface(float duration=1.5f);
    
    Anim<float>     mCrossFade;
    Anim<float>     mFade;
    Area            mBounds;
    gl::Texture     mTexture, mLastTexture;
    
};

FadingTexture::FadingTexture(){
    mFade = 1.0;
    mCrossFade = 0.0;
}

void FadingTexture::draw(){
    
	if( mLastTexture ) {
		gl::color( 1, 1, 1, (1.0f - mCrossFade)*mFade );
		Rectf textureBounds = mLastTexture.getBounds();
		Rectf drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mLastTexture, drawBounds );
	}
	if( mTexture ) {
		gl::color( 1, 1, 1, mCrossFade*mFade );
		Rectf textureBounds = mTexture.getBounds();
		Rectf drawBounds = textureBounds.getCenteredFit( mBounds, true );
		gl::draw( mTexture, drawBounds );
	}

}

void FadingTexture::fadeToSurface(float duration){
    timeline().apply( &mFade, 0.0f, duration );
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

class AtriumDisplayApp : public AppNative {
  public:
	void prepareSettings( Settings *settings );
	void setup();
	void mouseDown( MouseEvent event );
	void update();
	void draw();
	void shutdown();
    
    void loadImagesThreadFn();

    ConcurrentCircularBuffer<Surface>	*mSurfaces;

    gl::TextureFontRef	mTextureFontLight;
    gl::TextureFontRef	mTextureFontRegular;
    gl::TextureFontRef	mTextureFontBold;
    gl::TextureFontRef	mTextureFontSemiBold;
    gl::TextureFontRef	mTextureFontExtraBold;

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

};

void AtriumDisplayApp::prepareSettings( Settings *settings )
{
	settings->setWindowSize( 1300, 1300*(9./(16*3)) );
	settings->setFullScreen( false );
	settings->setResizable( false );
}

void AtriumDisplayApp::setup()
{
    
    if(abs((getDisplay()->getHeight()*1.f/getDisplay()->getWidth())-(9.f/(16.f*3.f))) < 0.1) {
        setWindowSize(getDisplay()->getWidth(), getDisplay()->getHeight());
        setFullScreen(true);
    } else {
        setWindowSize(getDisplay()->getWidth(), round(getDisplay()->getWidth()*(9./(16*3))));
    }
 
    // fonts
    gl::TextureFont::Format f;
    f.enableMipmapping( true );
    {
        Font customFont( Font( loadResource( RES_CUSTOM_FONT_REGULAR ), getWindowHeight()*.4 ) );
        mTextureFontRegular = gl::TextureFont::create( customFont, f );
    }
    {
        Font customFont( Font( loadResource( RES_CUSTOM_FONT_BOLD ), getWindowHeight()*.4 ) );
        mTextureFontBold = gl::TextureFont::create( customFont, f );
    }
    {
        Font customFont( Font( loadResource( RES_CUSTOM_FONT_SEMIBOLD ), getWindowHeight()*.4 ) );
        mTextureFontSemiBold = gl::TextureFont::create( customFont, f );
    }
    {
        Font customFont( Font( loadResource( RES_CUSTOM_FONT_LIGHT ), getWindowHeight()*.4 ) );
        mTextureFontLight = gl::TextureFont::create( customFont, f );
    }
    {
        Font customFont( Font( loadResource( RES_CUSTOM_FONT_EXTRABOLD ), getWindowHeight()*.4 ) );
        mTextureFontExtraBold = gl::TextureFont::create( customFont, f );
    }
    
	mShouldQuit = false;
	mSurfaces = new ConcurrentCircularBuffer<Surface>( 5 ); // room for 5 images
	
    mFullTexture.mBounds = getWindowBounds();
    mLeftTexture.mBounds.set(0, 0, getWindowWidth()/3.f, getWindowHeight());
    mMidTexture.mBounds.set(getWindowWidth()/3.f, 0, getWindowWidth()*2.f/3.f, getWindowHeight());
    mRightTexture.mBounds.set(getWindowWidth()*2.f/3.f, 0, getWindowWidth(), getWindowHeight());
    
	mLastTime = getElapsedSeconds();
    mTransitionState = 0; // app just started;
    mTransitionStateNext = 0; // app just started;
    mTitleFade = 0;
    mStripesFade = 0;
    mStripesSquareness = 1;
    mStripesPosition = Vec2f(0,0);
    triggerTransition();
}

void AtriumDisplayApp::loadImagesThreadFn()
{
	ci::ThreadSetup threadSetup; // instantiate this if you're talking to Cinder from a secondary thread
	vector<Url>	urls;
    
	// parse the image URLS from the XML feed and push them into 'urls'
	const Url sunFlickrGroup = Url( "http://api.flickr.com/services/feeds/groups_pool.gne?id=52242317293@N01&format=rss_200" );
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
                // create and launch the thread
                mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
                timeline().apply( &mTitleFade, float(mTitleFade), 1.0f, 1.5f,EaseInExpo() );
                timeline().appendTo(&mTitleFade,  1.0f, 0.0f, 3.0f, EaseOutSine()).delay(1.f).finishFn( triggerTransition );
                timeline().apply( &mStripesFade, 1.0f, 1.5f,EaseInOutQuad() ).delay(4.f);

                mTransitionStateNext = 1;
                break;
            case 1:
                timeline().apply( &mStripesSquareness, 0.0f, 2.0f,EaseOutBounce() );
                timeline().apply( &mStripesFade, 1.0f, 1.5f,EaseInOutQuad() );
                timeline().apply( &mStripesPosition, Vec2f(-1,0), 5.f,EaseInQuad() ).delay(2.0).finishFn( triggerTransition );
                timeline().apply( &mStripesNoise, 1.0f, 1.5f,EaseInOutQuad() );
                mTransitionStateNext = 4;
                break;
            case 2:
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
                    timeline().add(triggerTransition, getElapsedSeconds()+randFloat(2.f,5.f));

                    mFadedTexture = fadingTexture;

                } else {
                    mStripesSquareness = 1;
                    mStripesPosition = Vec2f(0,0);
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

                    timeline().apply( &mFade, 0.0f, 1.5f );
                    timeline().apply( &mStripesSquareness, 1.0f, 0.f).delay(2.5);
                    timeline().apply( &mStripesFade, 0.0f, 0.f);
                    timeline().apply( &mStripesPosition, Vec2f(0,0), 0.f);
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
	gl::color( Color::white() );
    
    mLeftTexture.draw();
    mMidTexture.draw();
    mRightTexture.draw();
    mFullTexture.draw();
    
    if(mTitleFade > 0){
        gl::color(1.,1.,1.,mTitleFade);
        Vec2f stringDims = mTextureFontBold->measureString( "INTER" );
        mTextureFontBold->drawString( "INTER", Vec2f((getWindowWidth()/3.)-stringDims.x, getWindowHeight()*0.6) );
        mTextureFontBold->drawString( "MEDIA", Vec2f((getWindowWidth()/3.), getWindowHeight()*0.6) );

        mTextureFontLight->drawString( "LAB", Vec2f((getWindowWidth()*2/3.), getWindowHeight()*0.6) );
        
    }
    
    if(mStripesFade > 0){
        gl::color(1.,.9,0., mStripesFade*0.33);

        vector<PolyLine2f> stripe;
        stripe.push_back( PolyLine2f() );
        stripe.back().push_back( Vec2f( lerp(getWindowHeight(),0,mStripesSquareness), 0 ) );
        stripe.back().push_back( Vec2f( getWindowWidth()/3., 0 ) );
        stripe.back().push_back( Vec2f( (getWindowWidth()/3.)-lerp(getWindowHeight(),0,mStripesSquareness), getWindowHeight() ) );
        stripe.back().push_back( Vec2f( 0, getWindowHeight()) );

        for (int i=0; i<5; i++) {
        gl::pushMatrices();
            gl::translate(Vec2f((i-3.f)*2,0));
        gl::translate(((Vec2f)mStripesPosition).x * getWindowWidth(), ((Vec2f)mStripesPosition).y * getWindowHeight());
        gl::drawSolid(stripe.back());
        gl::translate(getWindowWidth()/3., 0);
        gl::drawSolid(stripe.back());
        gl::translate(getWindowWidth()/3., 0);
        gl::drawSolid(stripe.back());
        gl::popMatrices();
        }
    }
    gl::color(.3,.3,.3);
    gl::drawLine(Vec2f(getWindowWidth()/3., 0), Vec2f(getWindowWidth()/3.,getWindowHeight()));
    gl::drawLine(Vec2f(getWindowWidth()*2/3., 0), Vec2f(getWindowWidth()*2/3.,getWindowHeight()));

}

void AtriumDisplayApp::shutdown()
{
	mShouldQuit = true;
	mSurfaces->cancel();
	mThread->join();
}

CINDER_APP_NATIVE( AtriumDisplayApp, RendererGl )
