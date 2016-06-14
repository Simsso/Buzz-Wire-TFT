#include <stdlib.h>

// screen
#include "SPI.h"
#include "Adafruit_GFX.h"
#include "Adafruit_ILI9341.h"

// touch
#include "TFT_Touch.h"

// custom label class
#include "Label.cpp"

#define DEBUG

enum GameState {
  Init,
  Start,
  GameTypeSelection,
  NameEntry,
  Running,
  Over,
  Ranks
};

enum GameType {
  Choice,
  Single,
  Multi
};


// PINNING

// screen pinning
const uint8_t TFT_RST = 8, TFT_DC = 9, TFT_CS = 10, TFT_MOSI = 11, TFT_MISO = 12, TFT_CLK = 13;

// touch pinning
const uint8_t DOUT = A0, DIN = A2, DCS = 7, DCLK = 6;

// buzz wire pinning
const uint8_t startStopPin = 2, mistakePin = 3;


// game constants
const unsigned long minGameTimeMillis = 1000, minTimeBetweenTwoMistakes = 1000, autoRestartMillis = 10000, minGameOverScreenMillis = minGameTimeMillis;

// screen constants
const int paddingLeft = 10; // for non-centered labels

const int BG_COLOR = ILI9341_BLACK;

String topRanksName[3];
double topRanksTime[3];

Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_MOSI, TFT_CLK, TFT_RST, TFT_MISO);
TFT_Touch touch = TFT_Touch(DCS, DCLK, DIN, DOUT);

Label *lblTitle, *lblSubtitle, 
  *lblPenaltyTimeTitle, *lblPenaltyTime, *lblPenaltyTimeContinue,
  *lblSingle, *lblMulti,
  *lblNameEntry, *lblNameInput,
  *lblTime, *lblMistakes, *lblTotal, *lblTimeValue, *lblMistakesValue, *lblTotalValue, 
  *lblGameOver;

const int numPenaltyTimeButtons = 5, penaltyTimeButtonsStepSize = 5, penaltyTimeMinValue = 5;
Label *penaltyTimeButtons[numPenaltyTimeButtons];

GameState gameState = GameState::Start, lastGameState = GameState::Init;

GameType gameType = GameType::Choice;

int penaltyTime = 5; // 5 seconds by default

// helper function to convert floating point numbers to strings
String floatToString(float x, byte precision = 2) {
  char tmp[50];
  dtostrf(x, 0, precision, tmp);
  return String(tmp);
}

int getPenaltyTimeByIndex(int i) {
  return i * penaltyTimeButtonsStepSize + penaltyTimeMinValue;
}

void setup()
{
  #ifdef DEBUG
    Serial.begin(9600);
    Serial.println("running...");
  #endif

  // buzz wire pins
  pinMode(startStopPin, INPUT_PULLUP);
  pinMode(mistakePin, INPUT_PULLUP);

  // init
  tft.begin();
  touch.setCal(3594, 733, 638, 3407, 320, 240, 1);

  // same rotation
  tft.setRotation(1);
  touch.setRotation(1);
  
  tft.fillScreen(ILI9341_BLACK);

  lblTitle = new Label(&tft, 10, ILI9341_RED, BG_COLOR, 3, "KreativeKiste.de");
  lblSubtitle = new Label(&tft, lblTitle->getBottomY() + 10, ILI9341_RED, BG_COLOR, 2, "Heisser Draht");

  // start
  lblPenaltyTimeTitle = new Label(&tft, lblSubtitle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 2, "Strafzeit einstellen", true);
  lblPenaltyTime = new Label(&tft, lblPenaltyTimeTitle->getBottomY() + 20, ILI9341_WHITE, BG_COLOR, 3, String(getPenaltyTimeByIndex(0)) + " s", true);

  for (int i = 0; i < numPenaltyTimeButtons; i++) {
    penaltyTimeButtons[i] = new Label(&tft, 
      (i == 0) ? 10 : penaltyTimeButtons[i - 1]->getRightX() + 20, // x
      lblPenaltyTime->getBottomY() + 20, // y
      ILI9341_WHITE, BG_COLOR, 2, String(getPenaltyTimeByIndex(i)) + " s", true);
  }
  lblPenaltyTimeContinue = new Label(&tft, penaltyTimeButtons[0]->getBottomY() + 20, ILI9341_WHITE, BG_COLOR, 2, "Weiter", true);

  // game type selection
  lblSingle = new Label(&tft, lblSubtitle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 3, "Singleplayer", true);
  lblMulti = new Label(&tft, lblSingle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 3, "Multiplayer", true);

  // name entry
  lblNameEntry = new Label(&tft, lblSubtitle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 2, "Bitte gib deinen Namen ein", true);
  lblNameInput = new Label(&tft, lblNameEntry->getBottomY() + 20, ILI9341_WHITE, BG_COLOR, 3, "", true);
  
  // running
  lblTime = new Label(&tft, paddingLeft, lblSubtitle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 3, "Zeit:", true);
  lblMistakes = new Label(&tft, paddingLeft, lblTime->getBottomY() + 10, ILI9341_WHITE, BG_COLOR, 3, "Fehler:", true);
  lblTotal = new Label(&tft, paddingLeft, lblMistakes->getBottomY() + 10, ILI9341_WHITE, BG_COLOR, 3, "Gesamt:", true);
  lblTimeValue = new Label(&tft, lblTotal->getRightX() + 10, lblSubtitle->getBottomY() + 30, ILI9341_WHITE, BG_COLOR, 3, "", true);
  lblMistakesValue = new Label(&tft, lblTotal->getRightX() + 10, lblTime->getBottomY() + 10, ILI9341_WHITE, BG_COLOR, 3, "", true);
  lblTotalValue = new Label(&tft, lblTotal->getRightX() + 10, lblMistakes->getBottomY() + 10, ILI9341_WHITE, BG_COLOR, 3, "", true);

  // over
  lblGameOver = new Label(&tft, lblTotal->getBottomY() + 30, ILI9341_BLUE, BG_COLOR, 3, "Game Over", true);
  
  delay(2000);
}

void loop()
{
  int touchX, touchY;
  bool touched = false;
  
  unsigned long startMillis, endMillis, lastMistake;
  unsigned int mistakesCount;

  #ifdef DEBUG
    unsigned long fpsCounterStart;
  #endif
  
  while (1) {
    // fps
    #ifdef DEBUG
      //Serial.print("Time (main loop): ");
      //Serial.println(millis() - fpsCounterStart);
      fpsCounterStart = millis();
    #endif

    // touch events
    if (touch.Pressed()) {
      touched = true;
      
      // read coords
      touchX = touch.X(); 
      touchY = touch.Y();
    }
    
    
    // START
    if (lastGameState != GameState::Start && gameState == GameState::Start) {
      #ifdef DEBUG
        Serial.println("GameState::Start");
      #endif
      lblPenaltyTimeTitle->show();
      lblPenaltyTime->show();
      for (int i = 0; i < numPenaltyTimeButtons; i++) {
        penaltyTimeButtons[i]->show();
      }
      lblPenaltyTimeContinue->show();

      // don't enter this if the next time
      lastGameState = GameState::Start;
    }
    if (gameState == GameState::Start) {
      lblPenaltyTime->setText(String(penaltyTime) + " s"); // updateLabel

      // penalty time button click check
      for (int i = 0; i < numPenaltyTimeButtons; i++) {
        if (touched && penaltyTimeButtons[i]->clicked(touchX, touchY)) {
          penaltyTimeButtons[i]->showBorder(ILI9341_WHITE);
          penaltyTime = getPenaltyTimeByIndex(i); 
        }
        else if (touched) {
          penaltyTimeButtons[i]->hideBorder();
        }
      }

      // exit loop
      if (touched && lblPenaltyTimeContinue->clicked(touchX, touchY)) { // LOW pin starts game
        setGameState(GameState::GameTypeSelection);
      }
    }
    if (lastGameState == GameState::Start && gameState != GameState::Start) {
      #ifdef DEBUG
        Serial.println("exiting GameState::Start");
      #endif
      lblPenaltyTimeTitle->hide();
      lblPenaltyTime->hide();
      for (int i = 0; i < numPenaltyTimeButtons; i++) {
        penaltyTimeButtons[i]->hide();
      }
      lblPenaltyTimeContinue->hide();
    }

    // GAME TYPE SELECTION
    if (lastGameState != GameState::GameTypeSelection && gameState == GameState::GameTypeSelection) {
      #ifdef DEBUG
        Serial.println("GameState::GameTypeSelection");
      #endif

      // show buttons
      lblSingle->show();
      lblMulti->show();

      // don't enter this if the next time
      lastGameState = GameState::GameTypeSelection;
    }
    if (gameState == GameState::GameTypeSelection) {
      if (gameType == GameType::Choice) { // not chosen yet
        // single clicked
        if (touched && lblSingle->clicked(touchX, touchY)) {
          lblSingle->showBorder(ILI9341_WHITE);
          gameType = GameType::Single;
        }
        else if (touched) {
          lblSingle->hideBorder();
        }
        
        // multi clicked
        if (touched && lblMulti->clicked(touchX, touchY)) {
          lblMulti->showBorder(ILI9341_WHITE);
          gameType = GameType::Multi;
        }
        else if (touched) {
          lblMulti->hideBorder();
        }
      }
      
      // exit loop to single player
      if (gameType == GameType::Single && !digitalRead(startStopPin)) { // LOW pin starts game
        setGameState(GameState::Running);
        startMillis = millis(); // save start millis as soon as possible
      }
      if (gameType == GameType::Multi) {
        setGameState(GameState::NameEntry);
      }
    }
    if (lastGameState == GameState::GameTypeSelection && gameState != GameState::GameTypeSelection) {
      #ifdef DEBUG
        Serial.println("exiting GameState::GameTypeSelection");
      #endif
      lblSingle->hide();
      lblMulti->hide();
    }
  
  
    // NAME ENTRY
    if (lastGameState != GameState::NameEntry && gameState == GameState::NameEntry) {
      #ifdef DEBUG
        Serial.println("GameState::NameEntry");
      #endif

      lblNameEntry->show();
      lblNameInput->show();
      
      // don't enter this if the next time
      lastGameState = GameState::NameEntry;
    }
    if (gameState == GameState::NameEntry) {
      // exit loop
    }
    if (lastGameState == GameState::NameEntry && gameState != GameState::NameEntry) {
      #ifdef DEBUG
        Serial.println("exiting GameState::NameEntry");
      #endif
      lblNameEntry->hide();
      lblNameInput->hide();
    }
  
  
    // RUNNING
    if (lastGameState != GameState::Running && gameState == GameState::Running) {
      #ifdef DEBUG
        Serial.println("GameState::Running");
      #endif
      
      lastMistake = 0;
      mistakesCount = 0;
      
      lblTime->show();
      lblMistakes->show();
      lblTotal->show();
      lblTimeValue->show();
      lblMistakesValue->show();
      lblTotalValue->show();

      // don't enter this if the next time
      lastGameState = GameState::Running;
    }
    if (gameState == GameState::Running) {
      double secondsElapsed = (double)(millis() - startMillis) / (double)1000;
      lblTimeValue->setText(floatToString(secondsElapsed, 1) + " s");
      lblMistakesValue->setText(String(mistakesCount));
      lblTotalValue->setText(floatToString(secondsElapsed + mistakesCount * penaltyTime, 1) + " s");
  
      // mistake detection
      if (!digitalRead(mistakePin) && (mistakesCount == 0 || millis() > lastMistake + minTimeBetweenTwoMistakes)) {
        mistakesCount++;
        lastMistake = millis();
      }
  
      // exit loop
      if (!digitalRead(startStopPin) && millis() > startMillis + minGameTimeMillis) { // LOW pin stops game and min time elapsed
        setGameState(GameState::Over);
      }
    }
    if (lastGameState == GameState::Running && gameState != GameState::Running) {
      #ifdef DEBUG
        Serial.println("exiting GameState::Running");
      #endif
    }
  
  
    // OVER
    if (lastGameState != GameState::Over && gameState == GameState::Over) {
      #ifdef DEBUG
        Serial.println("GameState::Over");
      #endif
      endMillis = millis();
      lblGameOver->show();

      // don't enter this if the next time
      lastGameState = GameState::Over;
    }
    if (gameState == GameState::Over) {
      if (millis() > endMillis + autoRestartMillis) {
        setGameState(GameState::Start);
      }
      // exit loop
      if (!digitalRead(startStopPin) && millis() > endMillis + minGameOverScreenMillis) { // LOW pin stops game over screen
        setGameState(GameState::Start);
      }
    }
    if (lastGameState == GameState::Over && gameState != GameState::Over) {
      #ifdef DEBUG
        Serial.println("exiting GameState::Over");
      #endif
      lblTime->hide();
      lblMistakes->hide();
      lblTotal->hide();
      lblTimeValue->hide();
      lblMistakesValue->hide();
      lblTotalValue->hide();
      lblGameOver->hide();
    }


    // end of loop
    touched = false; // prevent multiple clicks with old coords
  }
}

void setGameState(GameState newState) {
  lastGameState = gameState;
  gameState = newState;
}
