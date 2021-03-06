/*******************************************************************************************
*
*   raylib example 03b - Mouse input 
*
*   This example has been created using raylib 1.0 (www.raylib.com)
*   raylib is licensed under an unmodified zlib/libpng license (View raylib.h for details)
*
*   Copyright (c) 2013 Ramon Santamaria (Ray San - raysan@raysanweb.com)
*
********************************************************************************************/

#include "raylib.h"

int main()
{
    // Initialization
    //--------------------------------------------------------------------------------------
    int screenWidth = 800;
    int screenHeight = 450;
    
    Vector2 ballPosition = { -100.0, -100.0 };
    int counter = 0;
    int mouseX, mouseY;
    
    InitWindow(screenWidth, screenHeight, "raylib example 06 - mouse input");
    //---------------------------------------------------------------------------------------
    
    // Main game loop
    while (!WindowShouldClose())    // Detect window close button or ESC key
    {
        // Update
        //----------------------------------------------------------------------------------
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            mouseX = GetMouseX();
            mouseY = GetMouseY();
            
            ballPosition.x = (float)mouseX;
            ballPosition.y = (float)mouseY;
        }
        //----------------------------------------------------------------------------------
        
        // Draw
        //----------------------------------------------------------------------------------
        BeginDrawing();
        
            ClearBackground(RAYWHITE);
            
            DrawCircleV(ballPosition, 40, GOLD);
            
            DrawText("mouse click to draw the ball", 10, 10, 20, DARKGRAY);
        
        EndDrawing();
        //----------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    CloseWindow();        // Close window and OpenGL context
    //--------------------------------------------------------------------------------------
    
    return 0;
}