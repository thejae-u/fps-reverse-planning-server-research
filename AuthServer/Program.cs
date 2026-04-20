using System.Text;
using AuthServer.OpenApi;
using AuthServer.Services;
using Microsoft.AspNetCore.Authentication.JwtBearer;
using Microsoft.IdentityModel.Tokens;
using Microsoft.OpenApi;
using Scalar.AspNetCore;

var builder = WebApplication.CreateBuilder(args);

builder.Services.AddControllers();

builder.Services.AddCors(options =>
{
    options.AddPolicy("DevCors", policy =>
    {
        policy
            .AllowAnyOrigin()
            .AllowAnyHeader()
            .AllowAnyMethod();
    });
});

builder.Services.AddOpenApi("v1", options =>
{
    options.AddDocumentTransformer<BearerSecuritySchemeTransformer>();

});

builder.Services.AddSingleton<GlobalFields>();
builder.Services.AddSingleton<UserService>();
builder.Services.AddSingleton<JwtTokenService>();
builder.Services.AddSingleton<MatchService>();
builder.Services.AddHostedService<MatchWorker>();
builder.Services.AddLogging();

var jwtIssuer = builder.Configuration["Jwt:Issuer"];
var jwtAudience = builder.Configuration["Jwt:Audience"];
var jwtSecretKey = builder.Configuration["Jwt:SecretKey"];

if (string.IsNullOrEmpty(jwtIssuer) || string.IsNullOrEmpty(jwtAudience) || string.IsNullOrEmpty(jwtSecretKey))
{
    throw new InvalidOperationException("JWT appsettings not set");
}

builder.Services.AddAuthentication(JwtBearerDefaults.AuthenticationScheme)
    .AddJwtBearer(options =>
    {
        options.TokenValidationParameters = new TokenValidationParameters
        {
            ValidateIssuer = true,
            ValidIssuer = jwtIssuer,
            ValidateAudience = true,
            ValidAudience = jwtAudience,
            ValidateIssuerSigningKey = true,
            IssuerSigningKey = new SymmetricSecurityKey(Encoding.UTF8.GetBytes(jwtSecretKey)),
            ValidateLifetime = true,
            ClockSkew = TimeSpan.Zero
        };
    });

builder.Services.AddAuthorization();

var app = builder.Build();

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
    app.MapScalarApiReference("/scalar", options =>
    {
        options.WithTitle("AuthServer API Reference");
    });
}

app.UseCors("DevCors");

app.UseAuthentication();
app.UseAuthorization();

app.MapControllers();

app.MapGet("/ping", () => "AuthServer v1.0 - OK");
app.MapGet("/health", () => new { status = "healthy", timestamp = DateTime.UtcNow });
app.MapGet("/version", () => "AuthServer 0.0.5");
app.MapGet("/info", () => new
{
    info = "API Server for Native C++ Game Logic Server",
    detail = "Created thejaeu with Perplexity AI"
});

app.Run();
