//=============================================================================================
// Triangle with smooth color and interactive polyline 
//=============================================================================================
// NYILATKOZAT
// ---------------------------------------------------------------------------------------------
// Nev    : Szlovak Anna
// Neptun : OPOFGK
// ---------------------------------------------------------------------------------------------
// ezennel kijelentem, hogy a feladatot magam keszitettem, es ha barmilyen segitseget igenybe vettem vagy
// mas szellemi termeket felhasznaltam, akkor a forrast es az atvett reszt kommentekben egyertelmuen jeloltem.
// A forrasmegjeloles kotelme vonatkozik az eloadas foliakat es a targy oktatoi, illetve a
// grafhazi doktor tanacsait kiveve barmilyen csatornan (szoban, irasban, Interneten, stb.) erkezo minden egyeb
// informaciora (keplet, program, algoritmus, stb.). Kijelentem, hogy a forrasmegjelolessel atvett reszeket is ertem,
// azok helyessegere matematikai bizonyitast tudok adni. Tisztaban vagyok azzal, hogy az atvett reszek nem szamitanak
// a sajat kontribucioba, igy a feladat elfogadasarol a tobbi resz mennyisege es minosege alapjan szuletik dontes.
// Tudomasul veszem, hogy a forrasmegjeloles kotelmenek megsertese eseten a hazifeladatra adhato pontokat
// negativ elojellel szamoljak el es ezzel parhuzamosan eljaras is indul velem szemben.
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
		gl_Position =  vec4(vertexPosition.x, vertexPosition.y, 0, 1) * MVP; 		// transform to clipping space
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

//this class is 90% from the "Triangle with smooth color and interactive polyline"
class Camera {
	vec2 wCenter; // center in world coordinates
	vec2 wSize;   // width and height in world coordinates
public:
	Camera() : wCenter(0, 0), wSize(30, 30) { }

	mat4 V() { return TranslateMatrix(-wCenter); }
	mat4 P() { return ScaleMatrix(vec2(2 / wSize.x, 2 / wSize.y)); }

	mat4 Vinv() { return TranslateMatrix(wCenter); }
	mat4 Pinv() { return ScaleMatrix(vec2(wSize.x / 2, wSize.y / 2)); }

	void Zoom(float s) { wSize = wSize * s; }
	void Pan(vec2 t) { wCenter = wCenter + t; }
};

Camera camera;		// 2D camera
GPUProgram gpuProgram;	// vertex and fragment shaders

//this class was called LineStrip in the base program, I modified to fit the Curve
class Curve {
public:
	unsigned int		vao, vbo;	// vertex array object, vertex buffer object
	std::vector<vec3>   controlPoints; // interleaved data of coordinates and colors
	std::vector<float> ts; // knots
	std::vector<float>  vertexData; // interleaved data of coordinates and colors
	vec2			    wTranslate; // translation
	int selectedPointIndex;

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
	}

	virtual vec3 r(float t) = 0; //pure virtual, the approximations must calculate it themselves

	

	void Clear() {
		controlPoints.clear();
		ts.clear();
	}

	int ClosestIndex(float cX, float cY) {
		//find the closest point to the clicked location
		float threshold = 0.1f; // this can be adjusted
		int closestPointIndex = -1;
		vec4 mVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv() * Minv();
		for (unsigned int i = 0; i < controlPoints.size(); i++) {
			if (fabs(controlPoints[i].x - mVertex.x) < threshold && fabs(controlPoints[i].y - mVertex.y) < threshold)
				closestPointIndex = i;
		}
		if (closestPointIndex != -1) {
			return closestPointIndex;
		}
		else 
		{
			return -1;
		}
	}

	void UpdatePoint(float cX, float cY, int index) {
		vec4 mVertex = vec4(cX, cY, 0, 1) * camera.Pinv() * camera.Vinv() * Minv();
		controlPoints[index] = vec3(mVertex.x, mVertex.y, 0.0f);
	}
	//this spline draws itself a bit differently 
	void Draw() {
		if (controlPoints.size() > 0) {
			vertexData.clear();
			// generate the curve points
			int numSections = 100;
			for (unsigned int i = 0; i < controlPoints.size() - 1; i++) {
				for (unsigned int j = 0; j <= numSections; j++) {
					float t = ts[i] + (ts[i + 1] - ts[i]) * ((float)j / numSections); //evenly spaced between the two control points
					vec3 point = r(t);
					vertexData.push_back(point.x);
					vertexData.push_back(point.y);
					vertexData.push_back(1); // red
					vertexData.push_back(1); // green
					vertexData.push_back(0); // blue
				}
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
			glDrawArrays(GL_LINE_STRIP, 0, (controlPoints.size() - 1) * (numSections + 1));

			// draw the control points
			glPointSize(10.0f);
			glDrawArrays(GL_POINTS, (controlPoints.size() - 1) * (numSections + 1), controlPoints.size());
		}
	}
};

//this algorithm is from the ppt 
class Lagrange : public Curve {
public:

	float L(int i, float t) {
		float Li = 1.0f;
		for (unsigned int j = 0; j < controlPoints.size(); j++) {
			if (j != i)
				Li *= (t - ts[j]) / (ts[i] - ts[j]);
		}
		return Li;
	}

	void AddPoint(float cX, float cY) override {
		Curve::AddPoint(cX, cY);
		float ti = (float)(ts.size()) / (ts.size() + 1);
		printf("%f",ti);
		ts.push_back(ti);
	}

	vec3 r(float t) {
		vec3 rt(0, 0, 0);
		for (unsigned int i = 0; i < controlPoints.size(); i++) {
			rt = rt + controlPoints[i] * L(i, t);
		}
		return rt;
	}
	void Draw() {
		Curve::Draw();
	}
};

//this algorithm is from the ppt 
class Bezier : public Curve {
public:
	float B(int i, float t) {
		int n = controlPoints.size() - 1; // n+1 pts!
		float choose = 1;
		for (unsigned int j = 1; j <= i; j++) choose *= (float)(n - j + 1) / j;
		return choose * pow(t, i) * pow(1 - t, n - i);
	}
public:

	vec3 r(float t) override {
		vec3 rt(0, 0, 0);
		for (unsigned int i = 0; i < controlPoints.size(); i++)
			rt = rt + controlPoints[i] * B(i, t);
		return rt;
	}
	void Draw() {
		if (controlPoints.size() >= 0) {
			vertexData.clear();
			// generate the curve points
			int numSections = 100;
			for (unsigned int i = 0; i <= numSections; i++) {
				float t = (float)i / numSections;
				vec3 point = r(t);
				vertexData.push_back(point.x);
				vertexData.push_back(point.y);
				vertexData.push_back(1); // red
				vertexData.push_back(1); // green
				vertexData.push_back(0); // blue
			}

			// add control points to vertex data
			//because I want to display the points with red, and the curves with yellow
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

//this algorithm is from the ppt, and the Hermite is from the internet
class CatmullRom : public Curve {

public:
	float tension = 0.0f;

	vec3 Hermite(vec3 p0, vec3 v0, float t0, vec3 p1, vec3 v1, float t1, float t) {
		vec3 a0 = p0;
		vec3 a1 = v0;
		vec3 a2 = 3 * (p1 - p0) / pow((t1 - t0), 2) - (v1 + 2 * v0) / (t1 - t0);
		vec3 a3 = 2 * (p0 - p1) / pow((t1 - t0), 3) + (v1 + v0) / pow((t1 - t0), 2);
		return a0 + a1 * (t-t0) + a2 * pow((t - t0), 2) + a3 * pow((t - t0), 3);
	}

	vec3 r(float t) {
		for (int i = 0; i < controlPoints.size() - 1; i++) {
			if (ts[i] <= t && t <= ts[i + 1]) {
				vec3 v1; vec3 v0;
				if (i > 0)
					v0 = (1.0f - tension) * 0.5f * ((controlPoints[i + 1] - controlPoints[i]) / (ts[i + 1] - ts[i]) + (controlPoints[i] - controlPoints[i - 1]) / (ts[i] - ts[i - 1]));
				else
					v0 = (1.0f - tension) * 0.5f * (controlPoints[i + 1] - controlPoints[i]) / (ts[i + 1] - ts[i]);

				if (i < controlPoints.size() - 2) 
					v1 = (1.0f - tension) * 0.5f * ((controlPoints[i + 2] - controlPoints[i+1 ]) / (ts[i +2] - ts[i+1]) + (controlPoints[i+1] - controlPoints[i]) / (ts[i +1] - ts[i ]));
				else 
					v1 = (1.0f - tension) * 0.5f * (controlPoints[i + 1] - controlPoints[i]) / (ts[i + 1] - ts[i]);
				
				return Hermite(controlPoints[i], v0, ts[i], controlPoints[i + 1], v1, ts[i + 1], t);
			}
		}
		return vec3(0, 0, 0); // return zero vector if t is out of range
	}

	void AddPoint(float cX, float cY) override {
		Curve::AddPoint(cX, cY);
		if (controlPoints.size() == 1) {
			//the first knot is 0
			ts.push_back(0);
		}

		else {
			//for the rest of the knots, calculate the parameter value based on the distance to the previous
			vec3 diff = controlPoints.back() - controlPoints[controlPoints.size() - 2];
			float dist = sqrt(diff.x * diff.x + diff.y * diff.y);
			ts.push_back(pow(dist, tension) + ts.back());
		}
	}
	

	void Recalculate() {
		//clear the ts vector
		ts.clear();
		//recalculate the knots
		if (controlPoints.size() > 0) {
			ts.push_back(0);
			for (unsigned int i = 1; i < controlPoints.size(); i++) {
				vec3 diff = controlPoints[i] - controlPoints[i - 1];
				float dist = sqrt(diff.x * diff.x + diff.y * diff.y);
				ts.push_back(pow(dist, tension) + ts.back());
			}
		}
		//redraw the curve
		Draw();
		printf("Tension is now: %f\n", tension);
	}

	//when we press a key to begin to draw a new curve
	void Clear() {
		Curve::Clear();
		tension = 0.0f;
	}

	void Draw() {
		Curve::Draw();
	}
};

enum CurveType { NONE, BEZIER, LAGRANGE, CATMULLROM };
CurveType currentCurve = NONE;

Bezier bezier;
Lagrange lagrange;
CatmullRom catmullrom;

// Initialization, create an OpenGL context
void onInitialization() {
	glViewport(0, 0, 600, 600); 	// Position and size of the photograph on screen
	glLineWidth(2.0f); // Width of lines in pixels

	// Create objects by setting up their vertex data on the GPU
	bezier.create();
	lagrange.create();
	catmullrom.create();

	// create program for the GPU
	gpuProgram.create(vertexSource, fragmentSource, "fragmentColor");

	printf("\nUsage: \n");
	printf("Mouse Left Button: Add control point to polyline\n");
	printf("Key 'P': Camera pan -x\n");
	printf("Key 'p': Camera pan +x\n");
	printf("Key 'Z': Camera zoom in\n");
	printf("Key 'z': Camera zoom out\n");
	printf("Key 'b': Draw Bezier curve\n");
	printf("Key 'l': Draw Lagrange curve\n");
	printf("Key 'c': Draw CatmullRom spline\n");
	printf("Key 'T': CatmullRom spline tension increase by 0.1\n");
	printf("Key 't': CatmullRom spline tension decrease by 0.1\n");
}

// Window has become invalid: Redraw
void onDisplay() {
	glClearColor(0, 0, 0, 0);							// background color 
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); // clear the screen

	switch (currentCurve) {
	case BEZIER: bezier.Draw(); break;
	case LAGRANGE: lagrange.Draw(); break;
	case CATMULLROM: catmullrom.Draw(); break;
	case NONE: break;
	}
	glutSwapBuffers();									// exchange the two buffers
}

// Key of ASCII code pressed
void onKeyboard(unsigned char key, int pX, int pY) {
	switch (key) {
	case 'p': camera.Pan(vec2(-1, 0)); printf("Camera moved to the left 1 meter\n"); break;
	case 'P': camera.Pan(vec2(+1, 0)); printf("Camera moved to the right 1 meter\n"); break;

	case 'Z': camera.Zoom(1.1f); printf("Zoomed out\n"); break;
	case 'z': camera.Zoom(1/1.1); printf("Zoomed in\n"); break;

	case 'b': currentCurve = BEZIER;
		printf("Begin drawing Bezier\n");
		lagrange.Clear();
		catmullrom.Clear();
		break;
	case 'l': currentCurve = LAGRANGE;
		printf("Begin drawing Lagrange\n");
		bezier.Clear();
		catmullrom.Clear();
		break;
	case 'c': currentCurve = CATMULLROM;
		printf("Begin drawing Catmull-Rom\n");
		lagrange.Clear();
		bezier.Clear();
		break;

	case 'T': catmullrom.tension += 0.1f;
		catmullrom.Recalculate();
		printf("Tension increased by 0.1\n");
		break;
	case 't': catmullrom.tension -= 0.1f;
		catmullrom.Recalculate();
		printf("Tension decreased by 0.1\n");
		break;
	}
	glutPostRedisplay();
}

// Key of ASCII code released
void onKeyboardUp(unsigned char key, int pX, int pY) {
}

// Mouse click event
void onMouse(int button, int state, int pX, int pY) {
	float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
	float cY = 1.0f - 2.0f * pY / windowHeight;

	if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {  // GLUT_LEFT_BUTTON / GLUT_RIGHT_BUTTON and GLUT_DOWN / GLUT_UP

		switch (currentCurve) {
		case BEZIER: bezier.AddPoint(cX, cY);  printf("Point added at: %f, %f\n", cX, cY);  break;
		case LAGRANGE: lagrange.AddPoint(cX, cY);  printf("Point added at: %f, %f\n", cX, cY);  break;
		case CATMULLROM: catmullrom.AddPoint(cX, cY);   printf("Point added at: %f, %f\n", cX, cY); break;
		case NONE: break;
		}
	}
	else if (button == GLUT_RIGHT_BUTTON && state == GLUT_DOWN) {

		switch (currentCurve) {
		case BEZIER: bezier.selectedPointIndex = bezier.ClosestIndex(cX, cY);   break;
		case LAGRANGE: lagrange.selectedPointIndex = lagrange.ClosestIndex(cX, cY);  break;
		case CATMULLROM: catmullrom.selectedPointIndex = catmullrom.ClosestIndex(cX, cY);  break;
		case NONE: break;
		}
	}
	else if (state == GLUT_UP) {
		switch (currentCurve) {
		case BEZIER: bezier.selectedPointIndex = -1;   break;
		case LAGRANGE: lagrange.selectedPointIndex = -1;  break;
		case CATMULLROM: catmullrom.selectedPointIndex = -1;  break;
		case NONE: break;
		}
	}
	glutPostRedisplay();     // redraw
}

// Move mouse with key pressed
void onMouseMotion(int pX, int pY) {

	float cX = 2.0f * pX / windowWidth - 1;	// flip y axis
	float cY = 1.0f - 2.0f * pY / windowHeight;
	switch (currentCurve) {
	case BEZIER:  bezier.UpdatePoint(cX, cY, bezier.selectedPointIndex);  break;
	case LAGRANGE:  lagrange.UpdatePoint(cX, cY, lagrange.selectedPointIndex); break;
	case CATMULLROM:  catmullrom.UpdatePoint(cX, cY, catmullrom.selectedPointIndex); break;
	case NONE: break;
	}
	glutPostRedisplay();     // redraw
}

// Idle event indicating that some time elapsed: do animation here
void onIdle() {
	long time = glutGet(GLUT_ELAPSED_TIME); // elapsed time since the start of the program
}