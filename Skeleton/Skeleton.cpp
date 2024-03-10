//=============================================================================================
// Triangle with smooth color and interactive polyline 
//=============================================================================================
#include "framework.h"

// vertex shader in GLSL
const char* vertexSource = R"(
	#version 330				
    precision highp float;

	uniform mat4 MVP;			// Model-View-Projection matrix in row-major format

	layout(location = 0) in vec2 vertexPosition;	// Attrib Array 0
	layout(location = 1) in vec3 vertexColor;	    // Attrib Array 1
	
	out vec3 color;									// output attribute

	void main() {
		color = vertexColor;														// copy color from input to output
		gl_Position = vec4(vertexPosition.x, vertexPosition.y, 0, 1) * MVP; 		// transform to clipping space
	}
)";

// fragment shader in GLSL
const char* fragmentSource = R"(
	#version 330
    precision highp float;

	in vec3 color;				// variable input: interpolated color of vertex shader
	out vec4 fragmentColor;		// output that goes to the raster memory as told by glBindFragDataLocation

	void main() {
		fragmentColor = vec4(color, 1); // extend RGB to RGBA
	}
)";

class Camera {
	vec2 wCenter; // center in world coordinates
	vec2 wSize;   // width and height in world coordinates
public:
	Camera() : wCenter(0, 0), wSize(600, 600) { }

	mat4 V() { return TranslateMatrix(-wCenter); }
	mat4 P() { return ScaleMatrix(vec2(2 / wSize.x, 2 / wSize.y)); }

	mat4 Vinv() { return TranslateMatrix(wCenter); }
	mat4 Pinv() { return ScaleMatrix(vec2(wSize.x / 2, wSize.y / 2)); }

	void Zoom(float s) { wSize = wSize * s; }
	void Pan(vec2 t) { wCenter = wCenter + t; }
};

Camera camera;		// 2D camera
GPUProgram gpuProgram;	// vertex and fragment shaders

class Curve {
public:
	unsigned int		vao, vbo;	// vertex array object, vertex buffer object
	std::vector<vec3>   controlPoints; // interleaved data of coordinates and colors
	std::vector<float>  vertexData; // interleaved data of coordinates and colors
	vec2			    wTranslate; // translation

	void create() {
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);

		glGenBuffers(1, &vbo); // Generate 1 vertex buffer object
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		// Enable the vertex attribute arrays
		glEnableVertexAttribArray(0);  // attribute array 0
		glEnableVertexAttribArray(1);  // attribute array 1
		// Map attribute array 0 to the vertex data of the interleaved vbo
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(0)); // attribute array, components/attribute, component type, normalize?, stride, offset
		// Map attribute array 1 to the color data of the interleaved vbo
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));
	}

	mat4 M() { // modeling transform
		return mat4(1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			wTranslate.x, wTranslate.y, 0, 1); // translation
	}

	mat4 Minv() { // inverse modeling transform
		return mat4(1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			-wTranslate.x, -wTranslate.y, 0, 1); // inverse translation
	}

	virtual void AddPoint(float cX, float cY) {
		// input pipeline
		vec4 mVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv() * Minv();
		controlPoints.push_back(vec3(mVertex.x, mVertex.y, 0.0f));
		// fill interleaved data
		vertexData.push_back(mVertex.x);
		vertexData.push_back(mVertex.y);
		vertexData.push_back(1); // red
		vertexData.push_back(0); // green
		vertexData.push_back(0); // blue
		
	}
};

class Lagrange : public Curve {
public:

	std::vector<float> ts; // knots

	float L(int i, float t) {
		float Li = 1.0f;
		for (int j = 0; j < controlPoints.size(); j++) {
			if(j != i)
				Li *= (t - ts[j]) / (ts[i] - ts[j]);
		}
		return Li;
	}
public:
	void AddPoint(float cX, float cY) override {
		Curve::AddPoint(cX, cY);
		float ti = (float)controlPoints.size() / (controlPoints.size() + 1); // normalize ti
		printf("%f\n", ti);
		ts.push_back(ti);
	}

	vec3 r(float t) {
		vec3 rt(0, 0, 0);
		for (int i = 0; i < controlPoints.size(); i++) 
			rt = rt + controlPoints[i] * L(i, t);
		return rt;
	}
	void Draw() {
		if (controlPoints.size() > 0) {
			vertexData.clear();
			// generate the curve points
			int numSections = 100;
			for (int i = 0; i <= numSections; i++) {
				float t = (float)i / numSections;
				vec3 point = r(t);
				vertexData.push_back(point.x);
				printf("%f\n", point.x);
				vertexData.push_back(point.y);
				printf("%f\n", point.y);
				vertexData.push_back(1); // red
				vertexData.push_back(1); // green
				vertexData.push_back(0); // blue
			}

			// add control points to vertex data
			for (auto& point : controlPoints) {
				vertexData.push_back(point.x);
				vertexData.push_back(point.y);
				vertexData.push_back(1); // red
				vertexData.push_back(0); // green
				vertexData.push_back(0); // blue
			}

			// copy data to the GPU
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), &vertexData[0], GL_DYNAMIC_DRAW);

			// set GPU uniform matrix variable MVP with the content of CPU variable MVPTransform
			mat4 MVPTransform = M() * camera.V() * camera.P();
			gpuProgram.setUniform(MVPTransform, "MVP");

			// draw the curve
			glBindVertexArray(vao);
			glLineWidth(2.0f);
			glDrawArrays(GL_LINE_STRIP, 0, numSections + 1);

			// draw the control points
			glPointSize(10.0f);
			glDrawArrays(GL_POINTS, numSections + 1, controlPoints.size());
		}
	}
};

class Bezier : public Curve {
public:
	float B(int i, float t) {
		int n = controlPoints.size() - 1; // n+1 pts!
		float choose = 1;
		for (int j = 1; j <= i; j++) choose *= (float)(n - j + 1) / j;
			return choose * pow(t, i) * pow(1 - t, n - i);
	}
public:
	
	vec3 r(float t) {
		vec3 rt(0, 0, 0);
		for (int i = 0; i < controlPoints.size(); i++) 
			rt = rt + controlPoints[i] * B(i, t);
		return rt;
	}

	void Draw() {
		if (controlPoints.size() > 0) {
			printf("enter\n");
			vertexData.clear();
			// generate the curve points
			int numSections = 100;
			for (int i = 0; i <= numSections; i++) {
				float t = (float)i / numSections;
				vec3 point = r(t);
				vertexData.push_back(point.x);
				printf("%f\n", point.x);
				vertexData.push_back(point.y);
				printf("%f\n", point.y);
				vertexData.push_back(1); // red
				vertexData.push_back(1); // green
				vertexData.push_back(0); // blue
			}

			// add control points to vertex data
			for (auto& point : controlPoints) {
				vertexData.push_back(point.x);
				vertexData.push_back(point.y);
				vertexData.push_back(1); // red
				vertexData.push_back(0); // green
				vertexData.push_back(0); // blue
			}

			// copy data to the GPU
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), &vertexData[0], GL_DYNAMIC_DRAW);

			// set GPU uniform matrix variable MVP with the content of CPU variable MVPTransform
			mat4 MVPTransform = M() * camera.V() * camera.P();
			gpuProgram.setUniform(MVPTransform, "MVP");

			// draw the curve
			glBindVertexArray(vao);
			glLineWidth(2.0f);
			glDrawArrays(GL_LINE_STRIP, 0, numSections+1);

			// draw the control points
			glPointSize(10.0f);
			glDrawArrays(GL_POINTS, numSections + 1, controlPoints.size());
		}
	}

};
/*
class CatmullRom : public Curve {
public:
	std::vector<float> ts; // parameter (knot) values
	vec3 Hermite(vec3 p0, vec3 v0, float t0, vec3 p1, vec3 v1, float t1,
		float t) {
	}
public:
	void AddControlPoint(vec3 cp, float t) { controlPoints.push_back(cp); }
	vec3 r(float t) {
		for (int i = 0; i < controlPoints.size() - 1; i++)
			if (ts[i] <= t && t <= ts[i + 1]) {
				vec3 v0 = …, v1 = …;
				return Hermite(controlPoints[i], v0, ts[i], controlPoints[i + 1], v1, ts[i + 1], t);
			}
	}

};*/


Bezier bezier;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, windowWidth, windowHeight); 	// Position and size of the photograph on screen
	glLineWidth(2.0f); // Width of lines in pixels

	// Create objects by setting up their vertex data on the GPU
	bezier.create();

	// create program for the GPU
	gpuProgram.create(vertexSource, fragmentSource, "fragmentColor");

	printf("\nUsage: \n");
	printf("Mouse Left Button: Add control point to polyline\n");
	printf("Key 'P': Camera pan -x\n");
	printf("Key 'p': Camera pan +x\n");	
	printf("Key 'Z': Camera zoom in\n");
	printf("Key 'z': Camera zoom out\n");

}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 0, 0, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen
	
	//lineStrip.Draw();
	
	bezier.Draw();

	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	switch (key) {
	case 'P': camera.Pan(vec2(-1, 0)); break;
	case 'p': camera.Pan(vec2(+1, 0)); break;

	case 'Z': camera.Zoom(0.9f); break;
	case 'z': camera.Zoom(1.1f); break;

	}
	glutPostRedisplay();
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {  // GLUT_LEFT_BUTTON / GLUT_RIGHT_BUTTON and GLUT_DOWN / GLUT_UP
		float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
		float cY = 1.0f - 2.0f * pY / windowHeight;
		bezier.AddPoint(cX, cY);
		glutPostRedisplay();     // redraw
	}
}

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
	long time = glutGet(GLUT_ELAPSED_TIME); // elapsed time since the start of the program
	float sec = time / 1000.0f;				// convert msec to sec
	//glutPostRedisplay();					// redraw the scene
}
