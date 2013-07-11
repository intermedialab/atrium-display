PFont f;
PFont fb;

void setup() {
  size(displayWidth, displayHeight);
  f = createFont("OpenSans-Light.ttf", height*0.4, true);
  fb = createFont("OpenSans-ExtraBold.ttf", height*0.4, true);
  textFont(f);
}

void draw() {
  background(0);
  noStroke();
  
  float period = 20.0;
  float phaseShift = .5;
  float delta = millis()*0.001/period;
  float noiseDelta = (.5*cos((phaseShift+delta)*PI*2))+.5;
  float slideDelta = noiseDelta*((delta%2)-1)*1.85;
  float noiseDeltaPow2 = pow(noiseDelta,2);

  fill(255, 222, 0, 63+127/*abs(1-noiseDelta)*/);
  float segmentWidth = width / 6.;
  for (int i =0; i < width/height; i++) {
    float x = ((i-(i%2)) * segmentWidth) - (slideDelta*segmentWidth*7);
    beginShape();
    float noised = lerp(0, segmentWidth*7*(.5-noise(millis()*0.00012*.2, i*4, 5)), noiseDelta);
    vertex(x + noised + segmentWidth, 0);
    noised = lerp(0, segmentWidth*7*(.5-noise(millis()*0.00011*.2, i*4, 1.5)), noiseDelta);
    vertex(x + noised + segmentWidth*2, 0);
    noised = lerp(0, segmentWidth*7*(.5-noise(millis()*0.0001*.2, i*4, 0.444)), noiseDelta);
    vertex(x + noised + segmentWidth, height);
    noised = lerp(0, segmentWidth*7*(.5-noise(millis()*0.00014*.2, i*4, 200)), noiseDelta);
    vertex(x + noised, height);
    endShape();
  }

  fill(255, abs(noiseDeltaPow2)*255.);
  textFont(fb);
  textAlign(RIGHT, CENTER);
  text("INTER", width*(.99/3), height*.4);
  textAlign(LEFT, CENTER);
  text("MEDIA", width*(.99/3), height*.4);
  textFont(f);
  textAlign(LEFT, CENTER);
  text("LAB", width*(2./3), height*.4);
    
    
  fill(255, 222, 0, 63+127*abs(1-noiseDelta));

  for (int i =0; i < width/height; i++) {
    float x = ((i-(i%2)) * segmentWidth) - (slideDelta*segmentWidth*5);
    beginShape();
    float noised = lerp(0, segmentWidth*7*(.5-noise(millis()*0.00012*.2, i*4, 5)), noiseDeltaPow2);
    noised = lerp(segmentWidth, segmentWidth*10*(.5-noise(millis()*0.00012*.4, i*200, 51)), noiseDeltaPow2);
    vertex(x + noised, 0);
    noised = lerp(segmentWidth*2, segmentWidth*10*(.5-noise(millis()*0.00011*.4, i*200, 1.31)), noiseDeltaPow2);
    vertex(x + noised, 0);
    noised = lerp(segmentWidth, segmentWidth*10*(.5-noise(millis()*0.0001*.4, i*200, 234)), noiseDeltaPow2);
    vertex(x + noised, height);
    noised = lerp(0, segmentWidth*10*(.5-noise(millis()*0.00014*.4, i*200, 400)), noiseDeltaPow2);
    vertex(x + noised, height);
    endShape();
  }

/*
  fill(255,255);
  text(round(millis()*0.001), 10, height*.7);
  text(noiseDelta, width/3, height*.7);
  text(slideDelta, width*2/3, height*.7);
*/
}

boolean sketchFullScreen(){
  return true;
}

