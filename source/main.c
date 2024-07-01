#include <assert.h>
#include <curl/curl.h>
#include <curl/easy.h>
#include <jansson.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	RESPONSE_INITIAL_CAPACITY = 1024
};
struct response {
	char *buffer;
	size_t size, capacity;
};
size_t write_response(void *ptr, size_t size, size_t nmemb, void *userdata) {
	assert(ptr && size && userdata);

	struct response *response = userdata;

	if (nmemb >= SIZE_MAX / size || response->size >= SIZE_MAX - size * nmemb) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: buffer capacity overflowed\n",
			__FILE__,
			__LINE__,
			__func__
		);

		return 0;
	}

	if (response->size + nmemb * size >= response->capacity) {
		size_t capacity = response->capacity;
		do {
			if (!capacity) {
				capacity = 1;
			} else if (capacity > SIZE_MAX / 2) {
				capacity = SIZE_MAX;
			} else {
				capacity *= 2;
			}
		} while (response->size + nmemb * size >= capacity);

		char *buffer = realloc(response->buffer, capacity);
		if (!buffer) {
			(void)fprintf(
				stderr,
				"error: %s:%d: %s: failed to reallocate buffer capacity to %zu\n",
				__FILE__,
				__LINE__,
				__func__,
				capacity
			);

			return 0;
		}

		response->buffer = buffer;
		response->capacity = capacity;
	}

	memcpy(response->buffer + response->size, ptr, nmemb * size);
	response->size += nmemb * size;

	return nmemb * size;
}
char *request(const char *url) {
	assert(url);

	curl_global_init(CURL_GLOBAL_ALL);
	CURL *curl = curl_easy_init();
	if (!curl) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to initialize libcurl\n",
			__FILE__,
			__LINE__,
			__func__
		);

		return NULL;
	}

	struct response response;
	response.buffer = malloc(RESPONSE_INITIAL_CAPACITY);
	if (!response.buffer) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to allocate initial buffer capacity %d\n",
			__FILE__,
			__LINE__,
			__func__,
			RESPONSE_INITIAL_CAPACITY
		);

		goto error;
	}
	response.capacity = RESPONSE_INITIAL_CAPACITY;
	response.size = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_response);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

	CURLcode status = curl_easy_perform(curl);
	if (status != CURLE_OK) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to get data from %s: %s\n",
			__FILE__,
			__LINE__,
			__func__,
			url,
			curl_easy_strerror(status)
		);

		goto error;
	}

	long code = 0;
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	enum {
		RESPONSE_CODE_OK = 200
	};
	if (code != RESPONSE_CODE_OK) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: server responded with code %ld\n",
			__FILE__,
			__LINE__,
			__func__,
			code
		);

		goto error;
	}

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	char *buffer = realloc(response.buffer, response.size + 1);
	if (buffer) {
		response.buffer = buffer;
	}
	response.buffer[response.size] = '\0';

	return response.buffer;

error:
	free(response.buffer);

	curl_easy_cleanup(curl);
	curl_global_cleanup();

	return NULL;
}

#define LOCATION_URL "https://ipinfo.io/json"
struct location {
	double latitude, longitude;
};
struct location get_location(void) {
	char *response = request(LOCATION_URL);
	if (!response) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to get location\n",
			__FILE__,
			__LINE__,
			__func__
		);

		return (struct location){ 0 };
	}

	json_error_t error;
	json_t *root = json_loads(response, 0, &error);
	free(response);
	if (!root) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to parse json: %s\n",
			__FILE__,
			__LINE__,
			__func__,
			error.text
		);

		return (struct location){ 0 };
	}

	struct location location;
	// allow using sscanf to convert to float since we have formatted
	// data and we're checking if sscanf successed
	// NOLINTNEXTLINE(cert-err34-c)
	if (sscanf(
			json_string_value(json_object_get(root, "loc")),
			"%lf,%lf",
			&location.latitude,
			&location.longitude
		) != 2) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to parse location: %s\n",
			__FILE__,
			__LINE__,
			__func__,
			error.text
		);

		json_decref(root);

		return (struct location){ 0 };
	}

	json_decref(root);

	return location;
}

#define WEATHER_URL_FORMAT                                                                         \
	"https://api.open-meteo.com/v1/"                                                               \
	"forecast?latitude=%lg&longitude=%lg&timezone=auto&current=temperature_2m,apparent_"           \
	"temperature,relative_humidity_2m&daily=temperature_2m_max,temperature_2m_min&forecast_days=1"

int main(void) {
	struct location location = get_location();
	printf(
		"Location:\n\tLatitude: %lg\n\tLongitude: %g\n\n",
		location.latitude,
		location.longitude
	);

	size_t url_length =
		(size_t)snprintf(NULL, 0, WEATHER_URL_FORMAT, location.latitude, location.longitude);
	char *url = malloc(url_length + 1);
	if (!url) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to allocate buffer for url\n",
			__FILE__,
			__LINE__,
			__func__
		);

		return EXIT_FAILURE;
	}
	(void)snprintf(url, url_length + 1, WEATHER_URL_FORMAT, location.latitude, location.longitude);

	char *response = request(url);
	free(url);
	if (!response) {
		(void
		)fprintf(stderr, "error: %s:%d: %s: failed to get weather\n", __FILE__, __LINE__, __func__);

		return EXIT_FAILURE;
	}

	printf("Weather:\n");

	json_error_t error;
	json_t *root = json_loads(response, 0, &error);
	free(response);
	if (!root) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: failed to parse json: %d:%d: %s\n",
			__FILE__,
			__LINE__,
			__func__,
			error.line,
			error.column,
			error.text
		);

		return EXIT_FAILURE;
	}

	json_t *current = json_object_get(root, "current");
	if (!json_is_object(current)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: root doesn't contain current\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *current_units = json_object_get(root, "current_units");
	if (!json_is_object(current_units)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: root doesn't contain current_units\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}

	json_t *temperature = json_object_get(current, "temperature_2m");
	if (!json_is_number(temperature)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current doesn't contain temperature_2m\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *temperature_unit = json_object_get(current_units, "temperature_2m");
	if (!json_is_string(temperature_unit)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current_units doesn't contain temperature_2m\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	printf(
		"\tTemperature: %lg%s\n",
		json_number_value(temperature),
		json_string_value(temperature_unit)
	);

	json_t *apparent_temperature = json_object_get(current, "apparent_temperature");
	if (!json_is_number(apparent_temperature)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current doesn't contain apparent_temperature\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *apparent_temperature_unit = json_object_get(current_units, "apparent_temperature");
	if (!json_is_string(apparent_temperature_unit)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current_units doesn't contain apparent_temperature\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	printf(
		"\tApparent Temperature: %lg%s\n",
		json_number_value(apparent_temperature),
		json_string_value(apparent_temperature_unit)
	);

	json_t *daily = json_object_get(root, "daily");
	if (!json_is_object(daily)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: root doesn't contain daily\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *daily_units = json_object_get(root, "daily_units");
	if (!json_is_object(daily_units)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: root doesn't contain daily_units\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}

	json_t *maximum_temperature = json_array_get(json_object_get(daily, "temperature_2m_max"), 0);
	if (!json_is_number(maximum_temperature)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: daily doesn't contain temperature_2m_max\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *maximum_temperature_unit = json_object_get(daily_units, "temperature_2m_max");
	if (!json_is_string(maximum_temperature_unit)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: daily_units doesn't contain temperature_2m_max\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	printf(
		"\tMaximum Temperature: %lg%s\n",
		json_number_value(maximum_temperature),
		json_string_value(maximum_temperature_unit)
	);

	json_t *minimum_temperature = json_array_get(json_object_get(daily, "temperature_2m_min"), 0);
	if (!json_is_number(minimum_temperature)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: daily doesn't contain temperature_2m_min\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *minimum_temperature_unit = json_object_get(daily_units, "temperature_2m_min");
	if (!json_is_string(minimum_temperature_unit)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: daily_units doesn't contain temperature_2m_min\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	printf(
		"\tMinimum Temperature: %lg%s\n",
		json_number_value(minimum_temperature),
		json_string_value(minimum_temperature_unit)
	);

	json_t *relative_humidity = json_object_get(current, "relative_humidity_2m");
	if (!json_is_integer(relative_humidity)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current doesn't contain relative_humidity_2m\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	json_t *relative_humidity_unit = json_object_get(current_units, "relative_humidity_2m");
	if (!json_is_string(relative_humidity_unit)) {
		(void)fprintf(
			stderr,
			"error: %s:%d: %s: current_units doesn't contain relative_humidity_2m\n",
			__FILE__,
			__LINE__,
			__func__
		);

		json_decref(root);

		return EXIT_FAILURE;
	}
	printf(
		"\tRelative Humidity: %lld%s\n",
		json_integer_value(relative_humidity),
		json_string_value(relative_humidity_unit)
	);

	json_decref(root);

	return EXIT_SUCCESS;
}
