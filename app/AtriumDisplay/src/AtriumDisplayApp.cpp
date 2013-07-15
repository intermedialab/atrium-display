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
    Anim<float>             mLogoFade;
    Anim<float>             mHeaderFade;
    int                     mHeaderStringPos;
    vector<string>          mHeaderStrings;
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
    srandomdev();
    randSeed(random());
    perlin.setSeed(randInt());
    
	mShouldQuit = false;
	mSurfaces = new ConcurrentCircularBuffer<Surface>( 5 ); // room for 5 images

    // create and launch the thread
    // mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
    
    mFullTexture.mBounds = getWindowBounds();
    mLeftTexture.mBounds.set(0, 0, getWindowWidth()/3.f, getWindowHeight());
    mMidTexture.mBounds.set(getWindowWidth()/3.f, 0, getWindowWidth()*2.f/3.f, getWindowHeight());
    mRightTexture.mBounds.set(getWindowWidth()*2.f/3.f, 0, getWindowWidth(), getWindowHeight());
    
    mFullTexture.mColor = mLeftTexture.mColor = mMidTexture.mColor = mRightTexture.mColor = Color(1.f,.95f, .75f);
    
    mHeaderStrings.push_back("Projects");
    mHeaderStrings.push_back("A space for\nfull scale prototyping of\ncomputational environments.");
    mHeaderStrings.push_back("Adaptivity");
    mHeaderStrings.push_back("Architecture");
    mHeaderStrings.push_back("Research");
    mHeaderStringPos = 0;
	mLastTime = getElapsedSeconds();
    mTransitionState = 0; // app just started;
    mTransitionStateNext = 0; // app just started;
    mTitleFade = 0;
    mHeaderFade = 0;
    mLogoFade = 0;
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
	const Url sunFlickrGroup = Url( "http://api.flickr.com/services/feeds/photos_public.gne?tags=it,university,copenhagen,itu," + mHeaderStrings.at(mHeaderStringPos) + "&format=rss_200&tagmode=any" );
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
                mHeaderStringPos = (mHeaderStringPos+1)%mHeaderStrings.size();
                mThread = shared_ptr<thread>( new thread( bind( &AtriumDisplayApp::loadImagesThreadFn, this ) ) );
                
                timeline().apply( &mStripesSquareness, 0.0f, 0.f,EaseOutQuad() );
                timeline().apply( &mStripesPosition, Vec2f(1,0), 0.f,EaseInQuad() );
                timeline().apply( &mStripesNoise, .0f, .5f,EaseOutQuad() );
                timeline().apply( &mStripesFade, .9f, .5f,EaseInOutQuad() );
                timeline().apply( &mHeaderFade, 0.f, .5f,EaseInQuad() );
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
                    timeline().add(triggerTransition, getElapsedSeconds()+randFloat(3.5f,5.f));

                    mFadedTexture = fadingTexture;

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

                /* Rotating version
                 noiseX = mStripesNoise*segmentWidth*(5+i)*(perlin.noise(getElapsedSeconds()*.12*.25, 4*(i+1)*(j+1), 2));
        stripe.back().push_back( Vec2f( lerp(getWindowHeight()*1.f,(getWindowWidth()/3.f)+noiseX, mStripesSquareness)+noiseX, 0 ) );
                
        noiseX = mStripesNoise*segmentWidth*(5+i)*(perlin.noise(getElapsedSeconds()*.19*.25, 4*(i+1)*(j+1), 1.5));
        stripe.back().push_back( Vec2f( (getWindowWidth()/3.)+noiseX,lerp(0,getWindowHeight(),mStripesSquareness) ));
        
        noiseX = mStripesNoise*segmentWidth*(5+i)*(perlin.noise(getElapsedSeconds()*.13*.25, 4*(i+1)*(j+1), 37));
        stripe.back().push_back( Vec2f( ((getWindowWidth()/3.)-lerp(getWindowHeight()*1.f,getWindowWidth()/3.f, mStripesSquareness))+noiseX, getWindowHeight() ) );
        
        noiseX = mStripesNoise*segmentWidth*(5+i)*(perlin.noise(getElapsedSeconds()*.15*.25, 4*(i+1)*(j+1), 3.33));
        stripe.back().push_back( Vec2f( noiseX, lerp(getWindowHeight(),0,mStripesSquareness) ));
*/
                
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

    float margin = getWindowHeight()/8.f;

    if(mHeaderFade > 0){
        gl::color(0.,0.,0.,mHeaderFade);
        Vec2f stringDims = mTextureFontLight->measureString( mHeaderStrings.at(mHeaderStringPos) );
        gl::pushMatrices();
        float scale = fminf(.5f,((getWindowWidth()/3.f)-(margin*2.f))/stringDims.x);
        gl::translate(margin,(getWindowHeight()+(scale*mTextureFontLight->getDescent()))-(margin+((stringDims.y-mTextureFontLight->getAscent())*scale)));
        gl::scale(scale, scale);
        mTextureFontLight->drawString( mHeaderStrings.at(mHeaderStringPos) ,Vec2f(0.f, 0.f));
        gl::popMatrices();
        
    }

    if(mLogoFade > 0){
        
        gl::color(0.05,0.05,0.05,mLogoFade);
        
        Vec2f stringDims = mTextureFontLight->measureString( "IT UNIVERSITY OF COPENHAGEN" );
        
        float scale = ((getWindowWidth()/3.f)-(margin*2.5f))/stringDims.x;
        
        gl::drawSolidRect(Rectf((getWindowWidth()*2.f/3.f)+margin, getWindowHeight()-(margin+(stringDims.y*scale)), getWindowWidth()-margin, getWindowHeight()-margin));
        
        gl::color(1.,1.,1.,mLogoFade*mLogoFade);
        gl::pushMatrices();
        gl::translate((getWindowWidth()*2.f/3.f)+(1.25f*margin),getWindowHeight()-((1.25f*margin)));
        gl::scale(scale, scale);
        mTextureFontLight->drawString( "IT UNIVERSITY OF COPENHAGEN" ,Vec2f(0.f, 0.f));
        gl::popMatrices();
        

    }
    
        gl::color(1.,1.,1.,mTitleFade);
        Vec2f stringDims = mTextureFontBold->measureString( "INTER" );
        mTextureFontBold->drawString( "INTER", Vec2f((getWindowWidth()/3.)-(stringDims.x+10), getWindowHeight()*0.65) );
        mTextureFontBold->drawString( "MEDIA", Vec2f((getWindowWidth()/3.), getWindowHeight()*0.65) );
        
        mTextureFontLight->drawString( "LAB", Vec2f((getWindowWidth()*2/3.), getWindowHeight()*0.65) );
        

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

CINDER_APP_NATIVE( AtriumDisplayApp, RendererGl(RendererGl::AA_NONE) )
