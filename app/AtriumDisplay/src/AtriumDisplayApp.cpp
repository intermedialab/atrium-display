#include "cinder/app/AppNative.h"
#include "cinder/gl/gl.h"
#include "cinder/gl/Texture.h"
#include "cinder/Xml.h"
#include "cinder/Timeline.h"
#include "cinder/ImageIo.h"
#include "cinder/Thread.h"
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
	gl::Texture				mTexture, mLastTexture;
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
    setWindowSize(getDisplay()->getWidth(), round(getDisplay()->getWidth()*(9./(16*3))));
 
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
	// create and launch the thread
	mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
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
                timeline().apply( &mTitleFade, float(mTitleFade), 1.0f, 1.5f,EaseInExpo() );
                timeline().appendTo(&mTitleFade,  1.0f, 0.0f, 5.0f, EaseOutSine()).finishFn( triggerTransition );
                
                mTransitionStateNext = 1;
                break;
            case 1:
                timeline().apply( &mStripesSquareness, 0.0f, 2.0f,EaseInElastic(2, 1) );
                timeline().apply( &mStripesFade, 1.0f, 1.5f,EaseInOutQuad() );
                timeline().apply( &mStripesPosition, Vec2f(-1,0), 2.5f,EaseInOutQuad() ).delay(2.0).finishFn( triggerTransition );
                timeline().apply( &mStripesNoise, 1.0f, 1.5f,EaseInOutQuad() );
                mTransitionStateNext = 4;
                break;
            case 2:
                break;
            case 4:
                if( mSurfaces->isNotEmpty() ) {
                    mLastTexture = mTexture; // the "last" texture is now the current text
                    
                    Surface newSurface;
                    mSurfaces->popBack( &newSurface );
                    mTexture = gl::Texture( newSurface );
                    
                    mLastTime = getElapsedSeconds();
                    // blend from 0 to 1 over 1.5sec
                    timeline().apply( &mFade, 0.0f, 1.0f, 1.5f );
                    timeline().add(triggerTransition, getElapsedSeconds()+2.5);
                } else {
                    mStripesSquareness = 1;
                    mStripesPosition = Vec2f(0,0);
                    mTransitionStateNext = 0;
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
	
	if( mLastTexture ) {
		gl::color( 1, 1, 1, 1.0f - mFade );
		Rectf textureBounds = mLastTexture.getBounds();
		Rectf drawBounds = textureBounds.getCenteredFit( getWindowBounds(), true );
		gl::draw( mLastTexture, drawBounds );
	}
	if( mTexture ) {
		gl::color( 1, 1, 1, mFade );
		Rectf textureBounds = mTexture.getBounds();
		Rectf drawBounds = textureBounds.getCenteredFit( getWindowBounds(), true );
		gl::draw( mTexture, drawBounds );
	}

    if(mTitleFade > 0){
        gl::color(1.,1.,1.,mTitleFade);
        Vec2f stringDims = mTextureFontBold->measureString( "INTER" );
        mTextureFontBold->drawString( "INTER", Vec2f((getWindowWidth()/3.)-stringDims.x, getWindowHeight()*0.55) );
        mTextureFontBold->drawString( "MEDIA", Vec2f((getWindowWidth()/3.), getWindowHeight()*0.55) );

        mTextureFontLight->drawString( "LAB", Vec2f((getWindowWidth()*2/3.), getWindowHeight()*0.55) );
        
    }
    
    if(mStripesFade > 0){
        gl::color(1.,.9,0., mStripesFade);
        
        vector<PolyLine2f> stripe;
        stripe.push_back( PolyLine2f() );
        stripe.back().push_back( Vec2f( lerp(getWindowHeight(),0,mStripesSquareness), 0 ) );
        stripe.back().push_back( Vec2f( getWindowWidth()/3., 0 ) );
        stripe.back().push_back( Vec2f( (getWindowWidth()/3.)-lerp(getWindowHeight(),0,mStripesSquareness), getWindowHeight() ) );
        stripe.back().push_back( Vec2f( 0, getWindowHeight()) );
        
        gl::pushMatrices();
        gl::translate(((Vec2f)mStripesPosition).x * getWindowWidth(), ((Vec2f)mStripesPosition).y * getWindowHeight());
        gl::drawSolid(stripe.back());
        gl::translate(getWindowWidth()/3., 0);
        gl::drawSolid(stripe.back());
        gl::translate(getWindowWidth()/3., 0);
        gl::drawSolid(stripe.back());
        gl::popMatrices();
        
        
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
